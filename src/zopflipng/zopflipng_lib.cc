/*
Copyright 2016 Google Inc. All Rights Reserved.
Copyright 2016 Frédéric Kayser. All Rights Reserved.
Copyright 2016 Aaron Kaluszka. All Rights Reserved.
Copyright 2016 Mr_KrzYch00. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

Author: lode.vandevenne@gmail.com (Lode Vandevenne)
Author: jyrki.alakuijala@gmail.com (Jyrki Alakuijala)
Author: cryopng@free.fr (Frederic Kayser)

See zopflipng_lib.h
*/

#include "zopflipng_lib.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <set>
#include <vector>

#include "lodepng/lodepng.h"
#include "lodepng/lodepng_util.h"
#include "../zopfli/inthandler.h"
#include "../zopfli/deflate.h"
#include "../zopfli/util.h"

unsigned int mui;

ZopfliPNGOptions::ZopfliPNGOptions()
  : lossy_transparent(0)
  , lossy_8bit(false)
  , auto_filter_strategy(true)
  , use_zopfli(true)
  , num_iterations(15)
  , num_iterations_large(5)
  , block_split_strategy(1)
  , blocksplittingmax(15)
  , lengthscoremax(1024)
  , verbosezopfli(2)
  , lazymatching(0)
  , optimizehuffmanheader(0)
  , maxfailiterations(0)
  , findminimumrec(9)
  , ranstatew(1)
  , ranstatez(2)
  , usebrotli(0)
  , revcounts(0)
  , pass(0)
  , restorepoints(0)
  , noblocksplittinglast(0)
  , tryall(0)
  , slowsplit(0)
  , ga_population_size(19)
  , ga_max_evaluations(0)
  , ga_stagnate_evaluations(15)
  , ga_mutation_probability(0.01)
  , ga_crossover_probability(0.9)
  , ga_number_of_offspring(2)
  , numthreads(1)
  , cmwc(0) {
}

// Deflate compressor passed as fuction pointer to LodePNG to have it use Zopfli
// as its compression backend.
unsigned CustomPNGDeflate(unsigned char** out, size_t* outsize,
                          const unsigned char* in, size_t insize,
                          const LodePNGCompressSettings* settings) {
  const ZopfliPNGOptions* png_options =
      static_cast<const ZopfliPNGOptions*>(settings->custom_context);
  unsigned char bp = 0;
  ZopfliOptions options;
  ZopfliInitOptions(&options);

  options.numiterations         = insize < 200000
                                ? png_options->num_iterations
                                : png_options->num_iterations_large;
  options.blocksplittingmax     = png_options->blocksplittingmax;
  options.lengthscoremax        = png_options->lengthscoremax;
  options.verbose               = png_options->verbosezopfli;
  options.lazymatching          = png_options->lazymatching;
  options.optimizehuffmanheader = png_options->optimizehuffmanheader;
  mui                           = png_options->maxfailiterations;
  options.findminimumrec        = png_options->findminimumrec;
  options.ranstatew             = png_options->ranstatew;
  options.ranstatez             = png_options->ranstatez;
  options.usebrotli             = png_options->usebrotli;
  options.revcounts             = png_options->revcounts;
  options.pass                  = png_options->pass;
  options.restorepoints         = png_options->restorepoints;
  options.noblocksplittinglast  = png_options->noblocksplittinglast;
  options.tryall                = png_options->tryall;
  options.slowsplit             = png_options->slowsplit;
  options.numthreads            = png_options->numthreads;
  options.cmwc                  = png_options->cmwc;

  ZopfliDeflate(&options, 2 /* Dynamic */, 1, in, insize, &bp, out, outsize, 0);

  return 0;  // OK
}

// Returns 32-bit integer value for RGBA color.
static unsigned ColorIndex(const unsigned char* color) {
  return color[0] + 256u * color[1] + 65536u * color[2] + 16777216u * color[3];
}

// Counts amount of colors in the image, up to 257. If transparent_counts_as_one
// is enabled, any color with alpha channel 0 is treated as a single color with
// index 0.
void CountColors(std::set<unsigned>* unique,
                 const unsigned char* image, unsigned w, unsigned h,
                 bool transparent_counts_as_one) {
  unique->clear();
  for (size_t i = 0; i < w * h; i++) {
    unsigned index = ColorIndex(&image[i * 4]);
    if (transparent_counts_as_one && image[i * 4 + 3] == 0) index = 0;
    unique->insert(index);
    if (unique->size() > 256) break;
  }
}

int TryColorReduction(lodepng::State* inputstate, unsigned char* image,
    unsigned w, unsigned h) {
  // First look for binary (all or nothing) transparency color-key based.
  bool key = true;
  for (size_t i = 0; i < w * h; i++) {
    if (image[i * 4 + 3] > 0 && image[i * 4 + 3] < 255) {
      key = false;
      break;
    }
  }
  std::set<unsigned> count;  // Color count, up to 257.
  CountColors(&count, image, w, h, true);
  // Less than 257 colors means a palette could be used.
  bool palette = count.size() <= 256;

  // Choose the color key or first initial background color.
  if (key || palette) {
    int r = 0;
    int g = 0;
    int b = 0;
    for (size_t i = 0; i < w * h; i++) {
      if (image[i * 4 + 3] == 0) {
        // Use RGB value of first encountered transparent pixel. This can be
        // used as a valid color key, or in case of palette ensures a color
        // existing in the input image palette is used.
        r = image[i * 4 + 0];
        g = image[i * 4 + 1];
        b = image[i * 4 + 2];
        break;
      }
    }
    for (size_t i = 0; i < w * h; i++) {
      if (image[i * 4 + 3] == 0) {
        // if alpha is 0, set the RGB value to the sole color-key.
        image[i * 4 + 0] = r;
        image[i * 4 + 1] = g;
        image[i * 4 + 2] = b;
      }
    }
    if (palette) {
      // If there are now less colors, update palette of input image to match
      // this.
      if (palette && inputstate->info_png.color.palettesize > 0) {
        CountColors(&count, image, w, h, false);
        if (count.size() < inputstate->info_png.color.palettesize) {
          std::vector<unsigned char> palette_out;
          unsigned char* palette_in = inputstate->info_png.color.palette;
          for (size_t i = 0; i < inputstate->info_png.color.palettesize; i++) {
            if (count.count(ColorIndex(&palette_in[i * 4])) != 0) {
              palette_out.push_back(palette_in[i * 4 + 0]);
              palette_out.push_back(palette_in[i * 4 + 1]);
              palette_out.push_back(palette_in[i * 4 + 2]);
              palette_out.push_back(palette_in[i * 4 + 3]);
            }
          }
          inputstate->info_png.color.palettesize = palette_out.size() / 4;
          for (size_t i = 0; i < palette_out.size(); i++) {
            palette_in[i] = palette_out[i];
          }
        }
      }
      return 2;
    } else {
      return 1;
    }
  } else {
    return 0;
  }
}

// Remove RGB information from pixels with alpha=0 (does the same job as
// cryopng)
unsigned LossyOptimizeTransparent(unsigned char* image, unsigned w, unsigned h,
                                  int cleaner) {
  unsigned changes = 0;
  if (cleaner & 1) {  // None filter
    for (size_t i = 0; i < w * h; i++) {
      if (image[i * 4 + 3] == 0) {
        // if alpha is 0, set the RGB values to zero (black).
        if (changes == 0 && (image[i * 4 + 0] != 0 || image[i * 4 + 1] != 0
            || image[i * 4 + 2] != 0)) changes = 1;
        image[i * 4 + 0] = 0;
        image[i * 4 + 1] = 0;
        image[i * 4 + 2] = 0;
      }
    }
  } else if (cleaner & 2) {  // Sub filter
    int pr = 0;
    int pg = 0;
    int pb = 0;
    for (size_t i = 0; i < (4 * w * h); ) {
      for (size_t j = 3; j < 4 * w;) {
        // if alpha is 0, set the RGB values to those of the pixel on the left.
        if (image[i + j] == 0) {
          if (changes == 0 && (image[i + j - 3] != pr || image[i + j - 2] != pg
              || image[i + j - 1] != pb)) changes = 1;
          image[i + j - 3] = pr;
          image[i + j - 2] = pg;
          image[i + j - 1] = pb;
        } else {
          // Use the last encountered RGB value.
          pr = image[i + j - 3];
          pg = image[i + j - 2];
          pb = image[i + j - 1];
        }
        j += 4;
      }
      if (w > 1)
      {
        for (size_t j = 4 * (w - 2) + 3; j + 1 > 0;) {
          // if alpha is 0, set the RGB values to those of the pixel on the
          // right.
          if (image[i + j] == 0) {
            if (changes == 0 && (image[i + j - 3] != pr
                || image[i + j - 2] != pg
                || image[i + j - 1] != pb)) changes = 1;
            image[i + j - 3] = pr;
            image[i + j - 2] = pg;
            image[i + j - 1] = pb;
          } else {
            // Use the last encountered RGB value.
            pr = image[i + j - 3];
            pg = image[i + j - 2];
            pb = image[i + j - 1];
          }
          j -= 4;
        }
      }
      i += (w * 4);
      pr = pg = pb = 0;   // reset to zero at each new line
    }
  } else if (cleaner & 4) {  // Up filter
    for (size_t j = 3; j < 4 * w;) {
      // if alpha is 0, set the RGB values to zero (black), first line only.
      if (image[j] == 0) {
        if (changes == 0 && (image[j - 3] != 0 || image[j - 2] != 0
            || image[j - 1] != 0)) changes = 1;
        image[j - 3] = 0;
        image[j - 2] = 0;
        image[j - 1] = 0;
      }
      j += 4;
    }
    if (h > 1) {
      for (size_t j = 3; j < 4 * w;) {
        for (size_t i = w * 4; i < (4 * w * h); ) {
          // if alpha is 0, set the RGB values to those of the upper pixel.
          if (image[i + j] == 0) {
            if (changes == 0 && (image[i + j - 3] != image[i + j - 3 - 4 * w]
                || image[i + j - 2] != image[i + j - 2 - 4 * w]
                || image[i + j - 1] != image[i + j - 1 - 4 * w])) changes = 1;
            image[i + j - 3] = image[i + j - 3 - 4 * w];
            image[i + j - 2] = image[i + j - 2 - 4 * w];
            image[i + j - 1] = image[i + j - 1 - 4 * w];
          }
          i += (w * 4);
        }
        for (size_t i = 4 * w * (h - 2); i + w * 4 > 0;) {
          // if alpha is 0, set the RGB values to those of the lower pixel.
          if (image[i + j] == 0) {
            if (changes == 0 && (image[i + j - 3] != image[i + j - 3 + 4 * w]
                || image[i + j - 2] != image[i + j - 2 + 4 * w]
                || image[i + j - 1] != image[i + j - 1 + 4 * w])) changes = 1;
            image[i + j - 3] = image[i + j - 3 + 4 * w];
            image[i + j - 2] = image[i + j - 2 + 4 * w];
            image[i + j - 1] = image[i + j - 1 + 4 * w];
          }
          i -= (w * 4);
        }
        j += 4;
      }
    }
  } else if (cleaner & 8) {  // Average filter
    int pr = 0;
    int pg = 0;
    int pb = 0;
    for (size_t j = 3; j < 4*w;) {
      // if alpha is 0, set the RGB values to the half of those of the pixel on
      // the left, first line only.
      if (image[j] == 0) {
        pr = pr>>1;
        pg = pg>>1;
        pb = pb>>1;
        if (changes == 0 && (image[j - 3] != pr || image[j - 2] != pg
            || image[j - 1] != pb)) changes = 1;
        image[j - 3] = pr;
        image[j - 2] = pg;
        image[j - 1] = pb;
      } else {
        pr = image[j - 3];
        pg = image[j - 2];
        pb = image[j - 1];
      }
      j+=4;
    }
    if (h > 1) {
      for (size_t i = w*4; i < (4 * w * h); ) {
        pr = pg = pb = 0;   // reset to zero at each new line
        for (size_t j = 3; j < 4*w;) {
          // if alpha is 0, set the RGB values to the half of the sum of the
          // pixel on the left and the upper pixel.
          if (image[i + j] == 0) {
            pr = (pr+(int)image[i + j - (3 + 4*w)])>>1;
            pg = (pg+(int)image[i + j - (2 + 4*w)])>>1;
            pb = (pb+(int)image[i + j - (1 + 4*w)])>>1;
            if (changes == 0 && (image[i + j - 3] != pr
                || image[i + j - 2] != pg
                || image[i + j - 1] != pb)) changes = 1;
            image[i + j - 3] = pr;
            image[i + j - 2] = pg;
            image[i + j - 1] = pb;
          } else {
            pr = image[i + j - 3];
            pg = image[i + j - 2];
            pb = image[i + j - 1];
          }
          j+=4;
        }
        i+=(w*4);
      }
    }
  } else if (cleaner & 16) {  // Paeth filter
    int pre = 0;
    int pgr = 0;
    int pbl = 0;
    for (size_t j = 3; j < 4*w;) {  // First line (border effects)
      // if alpha is 0, alter the RGB value to a possibly more efficient one.
      if (image[j] == 0) {
        if (changes == 0 && (image[j - 3] != pre || image[j - 2] != pgr
            || image[j - 1] != pbl)) changes = 1;
        image[j - 3] = pre;
        image[j - 2] = pgr;
        image[j - 1] = pbl;
      } else {
        pre = image[j - 3];
        pgr = image[j - 2];
        pbl = image[j - 1];
      }
      j+=4;
    }
    if (h > 1) {
      int a, b, c, pa, pb, pc, p;
      for (size_t i = w*4; i < (4 * w * h); ) {
        pre = pgr = pbl = 0;   // reset to zero at each new line
        for (size_t j = 3; j < 4*w;) {
          // if alpha is 0, set the RGB values to the Paeth predictor.
          if (image[i + j] == 0) {
            if (j != 3) {  // not in first column
              a = pre;
              b = (int)image[i + j - (3 + 4*w)];
              c = (int)image[i + j - (7 + 4*w)];
              p = b - c;
              pc = a - c;
              pa = abs(p);
              pb = abs(pc);
              pc = abs(p + pc);
              pre = (pa <= pb && pa <=pc) ? a : (pb <= pc) ? b : c;

              a = pgr;
              b = (int)image[i + j - (2 + 4*w)];
              c = (int)image[i + j - (6 + 4*w)];
              p = b - c;
              pc = a - c;
              pa = abs(p);
              pb = abs(pc);
              pc = abs(p + pc);
              pgr = (pa <= pb && pa <=pc) ? a : (pb <= pc) ? b : c;

              a = pbl;
              b = (int)image[i + j - (1 + 4*w)];
              c = (int)image[i + j - (5 + 4*w)];
              p = b - c;
              pc = a - c;
              pa = abs(p);
              pb = abs(pc);
              pc = abs(p + pc);
              pbl = (pa <= pb && pa <=pc) ? a : (pb <= pc) ? b : c;

              if (changes == 0 && (image[i + j - 3] != pre
                  || image[i + j - 2] != pgr
                  || image[i + j - 1] != pbl)) changes = 1;
              image[i + j - 3] = pre;
              image[i + j - 2] = pgr;
              image[i + j - 1] = pbl;
            } else {
              // first column, set the RGB values to those of the upper pixel.
              pre = (int)image[i + j - (3 + 4*w)];
              pgr = (int)image[i + j - (2 + 4*w)];
              pbl = (int)image[i + j - (1 + 4*w)];
              if (changes == 0 && (image[i + j - 3] != pre
                  || image[i + j - 2] != pgr
                  || image[i + j - 1] != pbl)) changes = 1;
              image[i + j - 3] = pre;
              image[i + j - 2] = pgr;
              image[i + j - 1] = pbl;
            }
          } else {
            pre = image[i + j - 3];
            pgr = image[i + j - 2];
            pbl = image[i + j - 1];
          }
          j+=4;
        }
        i+=(w*4);
      }
    }
  } else if (cleaner & 32) {  // None filter (white)
    for (size_t i = 0; i < w * h; i++) {
      if (image[i * 4 + 3] == 0) {
        // if alpha is 0, set the RGB values to 255 (white).
        if (changes == 0 && (image[i * 4 + 0] != 255u
            || image[i * 4 + 1] != 255u
            || image[i * 4 + 2] != 255u)) changes = 1;
        image[i * 4 + 0] = 255u;
        image[i * 4 + 1] = 255u;
        image[i * 4 + 2] = 255u;
      }
    }
  }
  return changes;
}

// Tries to optimize given a single PNG filter strategy.
// Returns 0 if ok, other value for error
unsigned TryOptimize(
    const std::vector<unsigned char>& image, unsigned w, unsigned h,
    const lodepng::State& inputstate, bool bit16, bool keep_colortype,
    const std::vector<unsigned char>& origfile,
    ZopfliPNGFilterStrategy filterstrategy,
    bool use_zopfli, int windowsize, const ZopfliPNGOptions* png_options,
    std::vector<unsigned char>* out, unsigned char* filterbank) {
  unsigned error = 0;

  lodepng::State state;
  state.encoder.verbose = png_options->verbosezopfli;
  state.encoder.zlibsettings.windowsize = windowsize;
  state.encoder.zlibsettings.nicematch = 258;

  if (use_zopfli && png_options->use_zopfli) {
    state.encoder.zlibsettings.custom_deflate = CustomPNGDeflate;
    state.encoder.zlibsettings.custom_context = png_options;
  }

  if (keep_colortype) {
    state.encoder.auto_convert = 0;
    lodepng_color_mode_copy(&state.info_png.color, &inputstate.info_png.color);
  }
  if (inputstate.info_png.color.colortype == LCT_PALETTE) {
    // Make it preserve the original palette order
    lodepng_color_mode_copy(&state.info_raw, &inputstate.info_png.color);
    state.info_raw.colortype = LCT_RGBA;
    state.info_raw.bitdepth = 8;
  }
  if (bit16) {
    state.info_raw.bitdepth = 16;
  }

  state.encoder.filter_palette_zero = 0;

  std::vector<unsigned char> filters;
  switch (filterstrategy) {
    case kStrategyZero:
      state.encoder.filter_strategy = LFS_ZERO;
      break;
    case kStrategyMinSum:
      state.encoder.filter_strategy = LFS_MINSUM;
      break;
    case kStrategyDistinctBytes:
      state.encoder.filter_strategy = LFS_DISTINCT_BYTES;
      break;
    case kStrategyDistinctBigrams:
      state.encoder.filter_strategy = LFS_DISTINCT_BIGRAMS;
      break;
    case kStrategyEntropy:
      state.encoder.filter_strategy = LFS_ENTROPY;
      break;
    case kStrategyBruteForce:
      state.encoder.filter_strategy = LFS_BRUTE_FORCE;
      break;
    case kStrategyIncremental:
      state.encoder.filter_strategy = LFS_INCREMENTAL;
      break;
    case kStrategyGeneticAlgorithm:
      state.encoder.filter_strategy = LFS_GENETIC_ALGORITHM;
      state.encoder.predefined_filters = filterbank;
      state.encoder.ga.number_of_generations = png_options->ga_max_evaluations;
      state.encoder.ga.number_of_stagnations =
        png_options->ga_stagnate_evaluations;
      state.encoder.ga.population_size = png_options->ga_population_size;
      state.encoder.ga.mutation_probability =
        png_options->ga_mutation_probability;
      state.encoder.ga.crossover_probability =
        png_options->ga_crossover_probability;
      state.encoder.ga.number_of_offspring =
        std::min(png_options->ga_number_of_offspring,
                 png_options->ga_population_size);
      break;
    case kStrategyOne:
    case kStrategyTwo:
    case kStrategyThree:
    case kStrategyFour:
      // Set the filters of all scanlines to that number.
      filters.resize(h, filterstrategy);
      state.encoder.filter_strategy = LFS_PREDEFINED;
      state.encoder.predefined_filters = &filters[0];
      break;
    case kStrategyPredefined:
      lodepng::getFilterTypes(filters, origfile);
      if (filters.size() != h) return 1;  // Error getting filters
      state.encoder.filter_strategy = LFS_PREDEFINED;
      state.encoder.predefined_filters = &filters[0];
      break;
    default:
      break;
  }

  state.encoder.add_id = false;
  state.encoder.text_compression = 1;

  error = lodepng::encode(*out, image, w, h, state);

  // For very small output, also try without palette, it may be smaller thanks
  // to no palette storage overhead.
  if (!error && out->size() < 4096 && !keep_colortype) {
    lodepng::State teststate;
    std::vector<unsigned char> temp;
    lodepng::decode(temp, w, h, teststate, *out);
    if (teststate.info_png.color.colortype == LCT_PALETTE) {
      if (png_options->verbosezopfli) {
        printf("Palette was used,"
               " compressed result is small enough to also try RGB or grey.\n");
      }
      LodePNGColorProfile profile;
      lodepng_color_profile_init(&profile);
      lodepng_get_color_profile(&profile, &image[0], w, h, &state.info_raw);
      // Too small for tRNS chunk overhead.
      if (w * h <= 16 && profile.key) profile.alpha = 1;
      state.encoder.auto_convert = 0;
      state.info_png.color.colortype = (profile.alpha ? LCT_RGBA : LCT_RGB);
      state.info_png.color.bitdepth = 8;
      state.info_png.color.key_defined = (profile.key && !profile.alpha);
      if (state.info_png.color.key_defined) {
        state.info_png.color.key_defined = 1;
        state.info_png.color.key_r = (profile.key_r & 255u);
        state.info_png.color.key_g = (profile.key_g & 255u);
        state.info_png.color.key_b = (profile.key_b & 255u);
      }

      std::vector<unsigned char> out2;
      error = lodepng::encode(out2, image, w, h, state);
      if (out2.size() < out->size()) out->swap(out2);
    }
  }

  if (error) {
    printf("Encoding error %u: %s\n", error, lodepng_error_text(error));
    return error;
  }

  return 0;
}

// Outputs the intersection of keepnames and non-essential chunks which are in
// the PNG image.
void ChunksToKeep(const std::vector<unsigned char>& origpng,
                  const std::vector<std::string>& keepnames,
                  std::set<std::string>* result) {
  std::vector<std::string> names[3];
  std::vector<std::vector<unsigned char> > chunks[3];

  lodepng::getChunks(names, chunks, origpng);

  for (size_t i = 0; i < 3; i++) {
    for (size_t j = 0; j < names[i].size(); j++) {
      for (size_t k = 0; k < keepnames.size(); k++) {
        if (keepnames[k] == names[i][j]) {
          result->insert(names[i][j]);
        }
      }
    }
  }
}

// Keeps chunks with given names from the original png by literally copying them
// into the new png
void KeepChunks(const std::vector<unsigned char>& origpng,
                const std::vector<std::string>& keepnames,
                std::vector<unsigned char>* png) {
  std::vector<std::string> names[3];
  std::vector<std::vector<unsigned char> > chunks[3];

  lodepng::getChunks(names, chunks, origpng);
  std::vector<std::vector<unsigned char> > keepchunks[3];

  // There are 3 distinct locations in a PNG file for chunks: between IHDR and
  // PLTE, between PLTE and IDAT, and between IDAT and IEND. Keep each chunk at
  // its corresponding location in the new PNG.
  for (size_t i = 0; i < 3; i++) {
    for (size_t j = 0; j < names[i].size(); j++) {
      for (size_t k = 0; k < keepnames.size(); k++) {
        if (keepnames[k] == names[i][j]) {
          keepchunks[i].push_back(chunks[i][j]);
        }
      }
    }
  }

  lodepng::insertChunks(*png, keepchunks);
}

int ZopfliPNGOptimize(const std::vector<unsigned char>& origpng,
    const ZopfliPNGOptions& png_options,
    int verbose,
    std::vector<unsigned char>* resultpng) {
  // Use the largest possible deflate window size
  int windowsize = 32768;

  ZopfliPNGFilterStrategy filterstrategies[kNumFilterStrategies] = {
    kStrategyZero, kStrategyOne, kStrategyTwo, kStrategyThree, kStrategyFour,
    kStrategyMinSum, kStrategyDistinctBytes, kStrategyDistinctBigrams,
    kStrategyEntropy, kStrategyBruteForce, kStrategyIncremental,
    kStrategyPredefined, kStrategyGeneticAlgorithm
  };
  std::string strategy_name[kNumFilterStrategies] = {
    "zero", "one", "two", "three", "four", "minimum sum", "distinct bytes",
    "distinct bigrams", "entropy", "brute force", "incremental brute force",
    "predefined", "genetic algorithm"
  };
  const int pre_predefined = 10;
  unsigned strategy_enable = 0;
  if (png_options.filter_strategies.empty()) {
    strategy_enable = (1 << kNumFilterStrategies) - 1;
  }
  else {
    for (size_t i = 0; i < png_options.filter_strategies.size(); i++) {
      strategy_enable |=
        (1 << png_options.filter_strategies[filterstrategies[i]]);
    }
  }

  std::vector<unsigned char> image;
  unsigned w, h;
  unsigned error;
  lodepng::State inputstate;
  error = lodepng::decode(image, w, h, inputstate, origpng);

  bool keep_colortype = false;

  if (!png_options.keepchunks.empty()) {
    // If the user wants to keep the non-essential chunks bKGD or sBIT, the
    // input color type has to be kept since the chunks format depend on it.
    // This may severely hurt compression if it is not an ideal color type.
    // Ideally these chunks should not be kept for web images. Handling of bKGD
    // chunks could be improved by changing its color type but not done yet due
    // to its additional complexity, for sBIT such improvement is usually not
    // possible.
    std::set<std::string> keepchunks;
    ChunksToKeep(origpng, png_options.keepchunks, &keepchunks);
    keep_colortype = keepchunks.count("bKGD") || keepchunks.count("sBIT");
    if (keep_colortype && verbose) {
      printf("Forced to keep original color type due to keeping bKGD or sBIT"
             " chunk.\n");
    }
  }

  if (error) {
    if (verbose) {
      if (error == 1) {
        printf("Decoding error\n");
      } else {
        printf("Decoding error %u: %s\n", error, lodepng_error_text(error));
      }
    }
    return error;
  }

  bool bit16 = false;  // Using 16-bit per channel raw image
  if (inputstate.info_png.color.bitdepth == 16 &&
      (keep_colortype || !png_options.lossy_8bit)) {
    // Decode as 16-bit
    image.clear();
    error = lodepng::decode(image, w, h, origpng, LCT_RGBA, 16);
    bit16 = true;
  }

  if (!error) {
    std::vector<unsigned char> filter;
    std::vector<unsigned char> temp;
    std::vector<unsigned char> predefined;
    if (strategy_enable & (1 << kStrategyPredefined)) {
      lodepng::getFilterTypes(predefined, origpng);
    }
    size_t bestsize = SIZE_MAX;

    unsigned numcleaners = 1;
    // If all criteria met, use cleaners
    if (!bit16 && png_options.lossy_transparent > 0) {
      if (TryColorReduction(&inputstate, &image[0], w, h) == 0) numcleaners = 6;
    }

    unsigned bestcleaner = 0;
    for (unsigned j = 0; j < numcleaners; ++j) {
      unsigned cleaner = (1 << j);
      if (png_options.lossy_transparent > 0) {
        // If lossy_transparent, remove RGB information from pixels with alpha=0
        if (png_options.lossy_transparent & cleaner) {
          if (verbose) printf("Cleaning alpha using method %i\n", j);
          if (LossyOptimizeTransparent(&image[0], w, h, cleaner) == 0
              && cleaner > 1) continue;
        }
        else continue;
      }

      std::vector<unsigned char> filterbank;
      // initialize random filters for genetic algorithm
      if (strategy_enable & (1 << kStrategyGeneticAlgorithm)) {
        filterbank.resize(h * std::max(int(kNumFilterStrategies),
                                       png_options.ga_population_size));
        lodepng::randomFilter(filterbank);
      }

      for (int i = 0; i < kNumFilterStrategies; ++i) {
        if (!(strategy_enable & (1 << i))) continue;
        temp.clear();
        // If auto_filter_strategy, use fast compression to check which PNG
        // filter strategy gives the smallest output. This allows to then do
        // the slow and good compression only on that filter type.
        error = TryOptimize(image, w, h, inputstate, bit16, keep_colortype,
                            origpng, filterstrategies[i],
                            !png_options.auto_filter_strategy /* use_zopfli */,
                            windowsize, &png_options, &temp, &filterbank[0]);
        if (!error) {
          if (verbose) {
            printf("Filter strategy %s: %d bytes\n", strategy_name[i].c_str(),
                   (int) temp.size());
          }
          if ((strategy_enable & (1 << kStrategyPredefined)
              && i <= pre_predefined)
              || strategy_enable & (1 << kStrategyGeneticAlgorithm)) {
            lodepng::getFilterTypes(filter, temp);
          }
          // Skip predefined if already covered by another strategy
          if (strategy_enable & (1 << kStrategyPredefined)
              && i <= pre_predefined && predefined == filter) {
            strategy_enable &= ~(1 << kStrategyPredefined);
          }
          // Store filter for use in genetic algorithm seeding
          if (strategy_enable & (1 << kStrategyGeneticAlgorithm)) {
            std::copy(filter.begin(), filter.end(), filterbank.begin() + i * h);
          }
          if (temp.size() < bestsize) {
            bestsize = temp.size();
            bestcleaner = cleaner;
            (*resultpng).swap(temp);  // Store best result so far in the output.
          }
        }
      }
    }
    if (png_options.auto_filter_strategy) {
      temp.clear();
      if (png_options.lossy_transparent > 0) {
        LossyOptimizeTransparent(&image[0], w, h, bestcleaner);
      }
      error = TryOptimize(image, w, h, inputstate, bit16, keep_colortype,
                          *resultpng, kStrategyPredefined,
                          true /* use_zopfli */, windowsize, &png_options,
                          &temp, NULL);
      if (!error && temp.size() < bestsize) (*resultpng).swap(temp);
    }
  }

  if (!error) {
    if (!png_options.keepchunks.empty()) {
      KeepChunks(origpng, png_options.keepchunks, resultpng);
    }
  }

  return error;
}

extern "C" void CZopfliPNGSetDefaults(CZopfliPNGOptions* png_options) {

  memset(png_options, 0, sizeof(*png_options));
  // Constructor sets the defaults
  ZopfliPNGOptions opts;

  png_options->lossy_transparent        = opts.lossy_transparent;
  png_options->lossy_8bit               = opts.lossy_8bit;
  png_options->auto_filter_strategy     = opts.auto_filter_strategy;
  png_options->use_zopfli               = opts.use_zopfli;
  png_options->num_iterations           = opts.num_iterations;
  png_options->num_iterations_large     = opts.num_iterations_large;
  png_options->block_split_strategy     = opts.block_split_strategy;
  png_options->blocksplittingmax        = opts.blocksplittingmax;
  png_options->lengthscoremax           = opts.lengthscoremax;
  png_options->verbosezopfli            = opts.verbosezopfli;
  png_options->lazymatching             = opts.lazymatching;
  png_options->optimizehuffmanheader    = opts.optimizehuffmanheader;
  png_options->maxfailiterations        = opts.maxfailiterations;
  png_options->findminimumrec           = opts.findminimumrec;
  png_options->ranstatew                = opts.ranstatew;
  png_options->ranstatez                = opts.ranstatez;
  png_options->pass                     = opts.pass;
  png_options->restorepoints            = opts.restorepoints;
  png_options->noblocksplittinglast     = opts.noblocksplittinglast;
  png_options->tryall                   = opts.tryall;
  png_options->slowsplit                = opts.slowsplit;
  png_options->ga_population_size       = opts.ga_population_size;
  png_options->ga_max_evaluations       = opts.ga_max_evaluations;
  png_options->ga_stagnate_evaluations  = opts.ga_stagnate_evaluations;
  png_options->ga_mutation_probability  = opts.ga_mutation_probability;
  png_options->ga_crossover_probability = opts.ga_crossover_probability;
  png_options->ga_number_of_offspring   = opts.ga_number_of_offspring;
  png_options->numthreads               = opts.numthreads;
  png_options->cmwc                     = opts.cmwc;
}

extern "C" int CZopfliPNGOptimize(const unsigned char* origpng,
                                  const size_t origpng_size,
                                  const CZopfliPNGOptions* png_options,
                                  int verbose,
                                  unsigned char** resultpng,
                                  size_t* resultpng_size) {
  ZopfliPNGOptions opts;

  // Copy over to the C++-style struct
  opts.lossy_transparent        = png_options->lossy_transparent;
  opts.lossy_8bit               = !!png_options->lossy_8bit;
  opts.auto_filter_strategy     = !!png_options->auto_filter_strategy;
  opts.use_zopfli               = !!png_options->use_zopfli;
  opts.num_iterations           = png_options->num_iterations;
  opts.num_iterations_large     = png_options->num_iterations_large;
  opts.block_split_strategy     = png_options->block_split_strategy;
  opts.blocksplittingmax        = png_options->blocksplittingmax;
  opts.lengthscoremax           = png_options->lengthscoremax;
  opts.verbosezopfli            = png_options->verbosezopfli;
  opts.lazymatching             = png_options->lazymatching;
  opts.optimizehuffmanheader    = png_options->optimizehuffmanheader;
  opts.maxfailiterations        = png_options->maxfailiterations;
  opts.findminimumrec           = png_options->findminimumrec;
  opts.ranstatew                = png_options->ranstatew;
  opts.ranstatez                = png_options->ranstatez;
  opts.usebrotli                = png_options->usebrotli;
  opts.revcounts                = png_options->revcounts;
  opts.pass                     = png_options->pass;
  opts.restorepoints            = png_options->restorepoints;
  opts.noblocksplittinglast     = png_options->noblocksplittinglast;
  opts.tryall                   = png_options->tryall;
  opts.slowsplit                = png_options->slowsplit;
  opts.ga_population_size       = png_options->ga_population_size;
  opts.ga_max_evaluations       = png_options->ga_max_evaluations;
  opts.ga_stagnate_evaluations  = png_options->ga_stagnate_evaluations;
  opts.ga_mutation_probability  = png_options->ga_mutation_probability;
  opts.ga_crossover_probability = png_options->ga_crossover_probability;
  opts.ga_number_of_offspring   = png_options->ga_number_of_offspring;
  opts.numthreads               = png_options->numthreads;
  opts.cmwc                     = png_options->cmwc;


  for (int i = 0; i < png_options->num_filter_strategies; i++) {
    opts.filter_strategies.push_back(png_options->filter_strategies[i]);
  }

  for (int i = 0; i < png_options->num_keepchunks; i++) {
    opts.keepchunks.push_back(png_options->keepchunks[i]);
  }

  const std::vector<unsigned char> origpng_cc(origpng, origpng + origpng_size);
  std::vector<unsigned char> resultpng_cc;

  int ret = ZopfliPNGOptimize(origpng_cc, opts, verbose, &resultpng_cc);
  if (ret) {
    return ret;
  }

  *resultpng_size = resultpng_cc.size();
  *resultpng      = (unsigned char*) malloc(resultpng_cc.size());
  if (!(*resultpng)) {
    return ENOMEM;
  }

  memcpy(*resultpng,
         reinterpret_cast<unsigned char*>(&resultpng_cc[0]),
         resultpng_cc.size());

  return 0;
}
