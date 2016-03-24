/*
Copyright 2013 Google Inc. All Rights Reserved.
Copyright 2015 Mr_KrzYch00. All Rights Reserved.

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

/* __has_builtin available in clang */
#ifdef __has_builtin
# if __has_builtin(__builtin_ctz)
#   define HAS_BUILTIN_CTZ
# endif
/* __builtin_ctz available beginning with GCC 3.4 */
#elif __GNUC__ * 100 + __GNUC_MINOR__ >= 304
# define HAS_BUILTIN_CTZ
#endif

unsigned int mui;

ZopfliPNGOptions::ZopfliPNGOptions()
  : lossy_transparent(false)
  , alpha_cleaner(0)
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
  , pass(0) {
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

// Way faster using CTZ intrinsic
unsigned CountTrailingZeros(int x) {
#ifdef HAS_BUILTIN_CTZ
  return __builtin_ctz(x);
#else
  if (x == 0) {
    return 32;
  } else {
    unsigned n = 0;
    if ((x & 0x0000FFFF) == 0) {
      n += 16;
      x = x >> 16;
    }
    if ((x & 0x000000FF) == 0) {
      n += 8;
      x = x >> 8;
    }
    if ((x & 0x0000000F) == 0) {
      n += 4;
      x = x >> 4;
    }
    if ((x & 0x00000003) == 0) {
      n += 2;
      x = x >> 2;
    }
    if ((x & 0x00000001) == 0) {
      n += 1;
    }
    return n;
  }
#endif
}

// Prepare image for PNG-32 to PNG-24(+tRNS) or PNG-8(+tRNS) reduction.

// Remove RGB information from pixels with alpha=0
// ZopfliPNG implementation working as a substitute for TryColorReduction
// in CryoPNG alpha cleaner mode
int LossyOptimizeTransparent(lodepng::State* inputstate, unsigned char* image,
    unsigned w, unsigned h, int cleaner_enabled) {
  // First check if we want to preserve potential color-key background color,
  // or instead use the last encountered RGB value all the time to save bytes.
  bool key = true;
  for (size_t i = 0; i < w * h; i++) {
    if (image[i * 4 + 3] > 0 && image[i * 4 + 3] < 255) {
      key = false;
      break;
    }
  }
  std::set<unsigned> count;  // Color count, up to 257.
  CountColors(&count, image, w, h, true);
  // If true, means palette is possible so avoid using different RGB values for
  // the transparent color.
  bool palette = count.size() <= 256;

  // Choose the color key or first initial background color.
  int r = 0, g = 0, b = 0;
  if (key || palette) {
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
    if(cleaner_enabled>0) {
      for (size_t i = 0; i < w * h; i++) {
        if (image[i * 4 + 3] == 0) {
          // if alpha is 0, set the RGB value to the sole color-key.
          image[i * 4 + 0] = r;
          image[i * 4 + 1] = g;
          image[i * 4 + 2] = b;
        }
      }
      if (palette) {
        // If there are now less colors, update palette of input image to match this.
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
    }
  } else if(cleaner_enabled>0) {
    return 0;
  }

  for (size_t i = 0; i < w * h; i++) {
    // if alpha is 0, alter the RGB value to a possibly more efficient one.
    if (image[i * 4 + 3] == 0) {
      image[i * 4 + 0] = r;
      image[i * 4 + 1] = g;
      image[i * 4 + 2] = b;
    } else {
      if (!key && !palette) {
        // Use the last encountered RGB value if no key or palette is used: that
        // way more values can be 0 thanks to the PNG filter types.
        r = image[i * 4 + 0];
        g = image[i * 4 + 1];
        b = image[i * 4 + 2];
      }
    }
  }

  // If there are now less colors, update palette of input image to match this.
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
  return 0;
}

// Remove RGB information from pixels with alpha=0 (cryoPNG implementation)
void LossyOptimizeTransparentCryo(unsigned char* image, unsigned w, unsigned h, int cleaner) {
  if (cleaner & 1) {  // None filter
    for (size_t i = 0; i < w * h; i++) {
      if (image[i * 4 + 3] == 0) {
        // if alpha is 0, set the RGB values to zero (black).
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
          // if alpha is 0, set the RGB values to those of the pixel on the right.
          if (image[i + j] == 0) {
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
            image[i + j - 3] = image[i + j - 3 - 4 * w];
            image[i + j - 2] = image[i + j - 2 - 4 * w];
            image[i + j - 1] = image[i + j - 1 - 4 * w];
          }
          i += (w * 4);
        }
        for (size_t i = 4 * w * (h - 2); i + w * 4 > 0;) {
          // if alpha is 0, set the RGB values to those of the lower pixel.
          if (image[i + j] == 0) {
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
      // if alpha is 0, set the RGB values to the half of those of the pixel on the left,
      // first line only.
      if (image[j] == 0) {
        pr = pr>>1;
        pg = pg>>1;
        pb = pb>>1;
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
          // if alpha is 0, set the RGB values to the half of the sum of the pixel on the
          // left and the upper pixel.
          if (image[i + j] == 0) {
            pr = (pr+(int)image[i + j - (3 + 4*w)])>>1;
            pg = (pg+(int)image[i + j - (2 + 4*w)])>>1;
            pb = (pb+(int)image[i + j - (1 + 4*w)])>>1;
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

              image[i + j - 3] = pre;
              image[i + j - 2] = pgr;
              image[i + j - 1] = pbl;
            } else {  // first column, set the RGB values to those of the upper pixel.
              pre = (int)image[i + j - (3 + 4*w)];
              pgr = (int)image[i + j - (2 + 4*w)];
              pbl = (int)image[i + j - (1 + 4*w)];
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
  }
}

// Tries to optimize given a single PNG filter strategy.
// Returns 0 if ok, other value for error
unsigned TryOptimize(
    const std::vector<unsigned char>& image, unsigned w, unsigned h,
    const lodepng::State& inputstate, bool bit16,
    const std::vector<unsigned char>& origfile,
    ZopfliPNGFilterStrategy filterstrategy,
    bool use_zopfli, int windowsize, const ZopfliPNGOptions* png_options,
    std::vector<unsigned char>* out) {
  unsigned error = 0;

  lodepng::State state;
  state.encoder.zlibsettings.windowsize = windowsize;
  if (use_zopfli && png_options->use_zopfli) {
    state.encoder.zlibsettings.custom_deflate = CustomPNGDeflate;
    state.encoder.zlibsettings.custom_context = png_options;
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
    case kStrategyEntropy:
      state.encoder.filter_strategy = LFS_ENTROPY;
      break;
    case kStrategyBruteForce:
      state.encoder.filter_strategy = LFS_BRUTE_FORCE;
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
      if (filters.size() != h) return 1; // Error getting filters
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
  if (!error && out->size() < 4096) {
    lodepng::State teststate;
    std::vector<unsigned char> temp;
    lodepng::decode(temp, w, h, teststate, *out);
    if (teststate.info_png.color.colortype == LCT_PALETTE) {
      if (png_options->verbosezopfli!=0) {
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

// Use fast compression to check which PNG filter strategy gives the smallest
// output. This allows to then do the slow and good compression only on that
// filter type.
unsigned AutoChooseFilterStrategy(const std::vector<unsigned char>& image,
                                  unsigned w, unsigned h,
                                  const lodepng::State& inputstate, bool bit16,
                                  const std::vector<unsigned char>& origfile,
                                  int numstrategies,
                                  ZopfliPNGFilterStrategy* strategies,
                                  bool* enable) {
  std::vector<unsigned char> out;
  size_t bestsize = 0;
  int bestfilter = 0;

  // A large window size should still be used to do the quick compression to
  // try out filter strategies: which filter strategy is the best depends
  // largely on the window size, the closer to the actual used window size the
  // better.
  int windowsize = 32768;

  for (int i = 0; i < numstrategies; i++) {
    out.clear();
    unsigned error = TryOptimize(image, w, h, inputstate, bit16, origfile,
                                 strategies[i], false, windowsize, 0, &out);
    if (error) return error;
    if (bestsize == 0 || out.size() < bestsize) {
      bestsize = out.size();
      bestfilter = i;
    }
  }

  for (int i = 0; i < numstrategies; i++) {
    enable[i] = (i == bestfilter);
  }

  return 0;  /* OK */
}

// Use fast compression to check which PNG filter strategy gives the smallest
// output. This allows to then do the slow and good compression only on that
// filter type.
unsigned AutoChooseFilterStrategyCryo(const std::vector<unsigned char>& image,
                                  unsigned w, unsigned h,
                                  const lodepng::State& inputstate, bool bit16,
                                  const std::vector<unsigned char>& origfile,
                                  int numstrategies,
                                  ZopfliPNGFilterStrategy* strategies,
                                  unsigned int *enable,
                                  size_t *returnbestsize) {
  std::vector<unsigned char> out;
  size_t bestsize = 0;
  int bestfilter = 0;

  // A large window size should still be used to do the quick compression to
  // try out filter strategies: which filter strategy is the best depends
  // largely on the window size, the closer to the actual used window size the
  // better.
  int windowsize = 32768;

  for (int i = 0; i < numstrategies; i++) {
    out.clear();
    unsigned error = TryOptimize(image, w, h, inputstate, bit16, origfile,
                                 strategies[i], false, windowsize, 0, &out);
    if (error) return error;
    if (bestsize == 0 || out.size() < bestsize) {
      bestsize = out.size();
      bestfilter = i;
    }
  }

  *enable = 1<<bestfilter;
  *returnbestsize = bestsize;

  return 0;  /* OK */
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
    bool verbose,
    std::vector<unsigned char>* resultpng) {
  // Use the largest possible deflate window size
  int windowsize = 32768;

  ZopfliPNGFilterStrategy filterstrategies[kNumFilterStrategies] = {
    kStrategyZero, kStrategyOne, kStrategyTwo, kStrategyThree, kStrategyFour,
    kStrategyMinSum, kStrategyEntropy, kStrategyPredefined, kStrategyBruteForce
  };
  bool strategy_enable[kNumFilterStrategies] = {
    false, false, false, false, false, false, false, false, false
  };
  unsigned int strategy_enable_cryo = 0;
  std::string strategy_name[kNumFilterStrategies] = {
    "zero", "one", "two", "three", "four",
    "minimum sum", "entropy", "predefined", "brute force"
  };
  for (size_t i = 0; i < png_options.filter_strategies.size(); i++) {
    if(png_options.alpha_cleaner!=0) {
      strategy_enable_cryo |= 1<<png_options.filter_strategies[i];
    } else {
      strategy_enable[png_options.filter_strategies[i]] = true;
    }
  }

  std::vector<unsigned char> image;
  unsigned w, h;
  unsigned error;
  lodepng::State inputstate;
  error = lodepng::decode(image, w, h, inputstate, origpng);

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
  if (inputstate.info_png.color.bitdepth == 16 && !png_options.lossy_8bit) {
    // Decode as 16-bit
    image.clear();
    error = lodepng::decode(image, w, h, origpng, LCT_RGBA, 16);
    bit16 = true;
  }

  if (!error) {
    // If lossy_transparent, remove RGB information from pixels with alpha=0
    if (png_options.lossy_transparent && !bit16) {
      LossyOptimizeTransparent(&inputstate, &image[0], w, h, png_options.alpha_cleaner);
    } else if(png_options.alpha_cleaner!=0 && !bit16) {
      int oversizedcolortype;
      if (verbose) printf("No 16-bits components\n");
      oversizedcolortype = LossyOptimizeTransparent(&inputstate, &image[0], w, h, 0);
      if (oversizedcolortype == 2 && verbose) {
        printf("Less than 256 colors, will try paletted version.\n");
      }
      if (oversizedcolortype == 1 && verbose) printf("Full alpha not needed.\n");
      if ((oversizedcolortype == 0) && (png_options.alpha_cleaner > 0)) {
        printf("Starting alpha cleaning, with ");
        char numcleaners = 0;
        char bestcleaner = 0;
        char secondbestcleaner = 0;
        unsigned int bestfilter = 0;
        unsigned int secondbestfilter = 0;
        unsigned int tempfilter;
        int mode = 1;
        int maxcleaners = 5;
        size_t tempsize = 0;
        size_t bestsize = 0;
        size_t secondbestsize = 0;
        for(int i = 0; i<maxcleaners; ++i) {
          if (png_options.alpha_cleaner & mode) numcleaners++;
          mode <<= 1;
        }
        printf("%d cleaners\n", numcleaners);
        mode = 1;
        for(int i = 0; i<maxcleaners; ++i) {
          if (png_options.alpha_cleaner & mode) {
            if (verbose) printf("Cleaning alpha using method %i\n",i);
            LossyOptimizeTransparentCryo(&image[0], w, h, (png_options.alpha_cleaner & mode));
            if (png_options.auto_filter_strategy) {
              tempfilter = strategy_enable_cryo;
              error = AutoChooseFilterStrategyCryo(image, w, h, inputstate, bit16,
                                               origpng,
                                               /* Don't try brute force and predefined */
                                               kNumFilterStrategies - 2,
                                               filterstrategies, &tempfilter, &tempsize);
              if (!error) {
                if (verbose) {
                  printf("Best filter:%d, size:%d\n", CountTrailingZeros(tempfilter),
                         (unsigned int)tempsize);
                }
                if (bestsize == 0 || tempsize < bestsize) {
                  secondbestsize = bestsize;
                  secondbestfilter = bestfilter;
                  secondbestcleaner = bestcleaner;
                  bestsize = tempsize;
                  bestfilter = tempfilter;
                  bestcleaner = mode;
                } else if (secondbestsize == 0 || tempsize < secondbestsize) {
                  secondbestsize = tempsize;
                  secondbestfilter = tempfilter;
                  secondbestcleaner = mode;
                }
              }
            } else {
              for (int i = 0; i < kNumFilterStrategies; i++) {
                if (!(strategy_enable_cryo & (1<<i))) continue;

                std::vector<unsigned char> temp;
                error = TryOptimize(image, w, h, inputstate, bit16, origpng,
                            filterstrategies[i], true /* use_zopfli */,
                            windowsize, &png_options, &temp);
                if (!error) {
                  if (verbose) {
                    printf("Filter strategy %s: %d bytes\n",
                            strategy_name[i].c_str(), (int) temp.size());
                  }
                  if (bestsize == 0 || temp.size() < bestsize) {
                    bestsize = temp.size();
                    (*resultpng).swap(temp);  // Store best result so far in the output.
                  }
                }
              }
            }
          }
          mode <<= 1;
        }
        if (png_options.auto_filter_strategy) {
          LossyOptimizeTransparentCryo(&image[0], w, h,
                                   (png_options.alpha_cleaner & bestcleaner));
          strategy_enable_cryo = bestfilter;
          if (verbose) {
            printf("Best cleaner/filter/size: %d,%d,%d\n",
                   CountTrailingZeros(bestcleaner), CountTrailingZeros(bestfilter),
                   (unsigned int)bestsize);
            printf("Secondbest cleaner/filter/size: %d,%d,%d\n",
                   CountTrailingZeros(secondbestcleaner),
                   CountTrailingZeros(secondbestfilter), (unsigned int)secondbestsize);
          }
          bestsize = 0;
          for (int i = 0; i < kNumFilterStrategies; i++) {
            if (!(strategy_enable_cryo & (1<<i))) continue;

            std::vector<unsigned char> temp;
            error = TryOptimize(image, w, h, inputstate, bit16, origpng,
                                filterstrategies[i], true /* use_zopfli */,
                                windowsize, &png_options, &temp);
            if (!error) {
              if (verbose) {
                printf("Filter strategy %s: %d bytes\n",
                       strategy_name[i].c_str(), (int) temp.size());
              }
              if (bestsize == 0 || temp.size() < bestsize) {
                bestsize = temp.size();
                (*resultpng).swap(temp);  // Store best result so far in the output.
              }
            }
          }
        }
        if (!png_options.keepchunks.empty()) {
          KeepChunks(origpng, png_options.keepchunks, resultpng);
        }
        return error;
      }
    }

    if (png_options.auto_filter_strategy) {
      if(png_options.alpha_cleaner!=0) {
        size_t tempsize = 0;
        error = AutoChooseFilterStrategyCryo(image, w, h, inputstate, bit16,
                                         origpng,
                                         /* Don't try brute force */
                                         kNumFilterStrategies - 1,
                                         filterstrategies, &strategy_enable_cryo, &tempsize);
      } else {
        error = AutoChooseFilterStrategy(image, w, h, inputstate, bit16,
                                         origpng,
                                         /* Don't try brute force */
                                         kNumFilterStrategies - 1,
                                         filterstrategies, strategy_enable);
      }
    }
  }

  if (!error) {
    size_t bestsize = 0;

    for (int i = 0; i < kNumFilterStrategies; i++) {
      if(png_options.alpha_cleaner!=0) {
        if (!(strategy_enable_cryo & (1<<i))) continue;
      } else {
        if (!strategy_enable[i]) continue;
      }

      std::vector<unsigned char> temp;
      error = TryOptimize(image, w, h, inputstate, bit16, origpng,
                          filterstrategies[i], true /* use_zopfli */,
                          windowsize, &png_options, &temp);
      if (!error) {
        if (verbose) {
          printf("Filter strategy %s: %d bytes\n",
                 strategy_name[i].c_str(), (int) temp.size());
        }
        if (bestsize == 0 || temp.size() < bestsize) {
          bestsize = temp.size();
          (*resultpng).swap(temp);  // Store best result so far in the output.
        }
      }
    }

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

  png_options->lossy_transparent     = opts.lossy_transparent;
  png_options->lossy_8bit            = opts.lossy_8bit;
  png_options->auto_filter_strategy  = opts.auto_filter_strategy;
  png_options->use_zopfli            = opts.use_zopfli;
  png_options->alpha_cleaner         = opts.alpha_cleaner;
  png_options->num_iterations        = opts.num_iterations;
  png_options->num_iterations_large  = opts.num_iterations_large;
  png_options->block_split_strategy  = opts.block_split_strategy;
  png_options->blocksplittingmax     = opts.blocksplittingmax;
  png_options->lengthscoremax        = opts.lengthscoremax;
  png_options->verbosezopfli         = opts.verbosezopfli;
  png_options->lazymatching          = opts.lazymatching;
  png_options->optimizehuffmanheader = opts.optimizehuffmanheader;
  png_options->maxfailiterations     = opts.maxfailiterations;
  png_options->findminimumrec        = opts.findminimumrec;
  png_options->ranstatew             = opts.ranstatew;
  png_options->ranstatez             = opts.ranstatez;
  png_options->pass                  = opts.pass;
}

extern "C" int CZopfliPNGOptimize(const unsigned char* origpng,
                                  const size_t origpng_size,
                                  const CZopfliPNGOptions* png_options,
                                  int verbose,
                                  unsigned char** resultpng,
                                  size_t* resultpng_size) {
  ZopfliPNGOptions opts;

  // Copy over to the C++-style struct
  opts.lossy_transparent     = !!png_options->lossy_transparent;
  opts.lossy_8bit            = !!png_options->lossy_8bit;
  opts.auto_filter_strategy  = !!png_options->auto_filter_strategy;
  opts.use_zopfli            = !!png_options->use_zopfli;
  opts.alpha_cleaner         = png_options->alpha_cleaner;
  opts.num_iterations        = png_options->num_iterations;
  opts.num_iterations_large  = png_options->num_iterations_large;
  opts.block_split_strategy  = png_options->block_split_strategy;
  opts.blocksplittingmax     = png_options->blocksplittingmax;
  opts.lengthscoremax        = png_options->lengthscoremax;
  opts.verbosezopfli         = png_options->verbosezopfli;
  opts.lazymatching          = png_options->lazymatching;
  opts.optimizehuffmanheader = png_options->optimizehuffmanheader;
  opts.maxfailiterations     = png_options->maxfailiterations;
  opts.findminimumrec        = png_options->findminimumrec;
  opts.ranstatew             = png_options->ranstatew;
  opts.ranstatez             = png_options->ranstatez;
  opts.usebrotli             = png_options->usebrotli;
  opts.revcounts             = png_options->revcounts;
  opts.pass                  = png_options->pass;

  for (int i = 0; i < png_options->num_filter_strategies; i++) {
    opts.filter_strategies.push_back(png_options->filter_strategies[i]);
  }

  for (int i = 0; i < png_options->num_keepchunks; i++) {
    opts.keepchunks.push_back(png_options->keepchunks[i]);
  }

  const std::vector<unsigned char> origpng_cc(origpng, origpng + origpng_size);
  std::vector<unsigned char> resultpng_cc;

  int ret = ZopfliPNGOptimize(origpng_cc, opts, !!verbose, &resultpng_cc);
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
