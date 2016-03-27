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

Library to recompress and optimize PNG images. Uses Zopfli as the compression
backend, chooses optimal PNG color model, and tries out several PNG filter
strategies.
*/

#ifndef ZOPFLIPNG_LIB_H_
#define ZOPFLIPNG_LIB_H_

#ifdef __cplusplus

#include <string>
#include <vector>

extern "C" {

#endif

#include <stdlib.h>

enum ZopfliPNGFilterStrategy {
  kStrategyZero = 0,
  kStrategyOne = 1,
  kStrategyTwo = 2,
  kStrategyThree = 3,
  kStrategyFour = 4,
  kStrategyMinSum,
  kStrategyEntropy,
  kStrategyPredefined,
  kStrategyBruteForce,
  kNumFilterStrategies /* Not a strategy but used for the size of this enum */
};

typedef struct CZopfliPNGOptions {
  int lossy_transparent;

  int alpha_cleaner;

  int lossy_8bit;

  enum ZopfliPNGFilterStrategy* filter_strategies;
  // How many strategies to try.
  int num_filter_strategies;

  int auto_filter_strategy;

  char** keepchunks;
  // How many entries in keepchunks.
  int num_keepchunks;

  int use_zopfli;

  int num_iterations;

  int num_iterations_large;

  int block_split_strategy;

  int blocksplittingmax;

  int lengthscoremax;

  int verbosezopfli;

  int lazymatching;

  int optimizehuffmanheader;

  int maxfailiterations;

  unsigned int findminimumrec;

  unsigned short ranstatew;
  unsigned short ranstatez;

  int usebrotli;

  int revcounts;

  int pass;

  int restorepoints;

  int noblocksplittinglast;

  int tryall;

  int slowsplit;
} CZopfliPNGOptions;

// Sets the default options
// Does not allocate or set keepchunks or filter_strategies
void CZopfliPNGSetDefaults(CZopfliPNGOptions *png_options);

// Returns 0 on success, error code otherwise
// The caller must free resultpng after use
int CZopfliPNGOptimize(const unsigned char* origpng,
    const size_t origpng_size,
    const CZopfliPNGOptions* png_options,
    int verbose,
    unsigned char** resultpng,
    size_t* resultpng_size);

#ifdef __cplusplus
}  // extern "C"
#endif

// C++ API
#ifdef __cplusplus

struct ZopfliPNGOptions {
  ZopfliPNGOptions();

  // Allow altering hidden colors of fully transparent pixels
  bool lossy_transparent;

  // Keep above for backwards compatibility and expose Cryopng
  // cleaner separatelly.
  int alpha_cleaner;

  // Convert 16-bit per channel images to 8-bit per channel
  bool lossy_8bit;

  // Filter strategies to try
  std::vector<ZopfliPNGFilterStrategy> filter_strategies;

  // Automatically choose filter strategy using less good compression
  bool auto_filter_strategy;

  // PNG chunks to keep
  // chunks to literally copy over from the original PNG to the resulting one
  std::vector<std::string> keepchunks;

  // Use Zopfli deflate compression
  bool use_zopfli;

  // Zopfli number of iterations
  int num_iterations;

  // Zopfli number of iterations on large images
  int num_iterations_large;

  // Unused, left for backwards compatiblity.
  int block_split_strategy;

  // Maximum amount of blocks to split into (0 for unlimited, but this can give
  // extreme results that hurt compression on some files). Default value: 15.
  int blocksplittingmax;

  // Used to alter GetLengthScore max distance, this affects block splitting
  // model and the chance for first run being closer to the optimal output.
  int lengthscoremax;

  // How much zopfli output to print, verbose level
  int verbosezopfli;

  // Enable lazy matching in LZ77 Greedy may provide various results for different files when enabled.
  int lazymatching;

  /*
  Work by Frédéric Kayser in his Zopfli fork: https://github.com/frkay/zopfli
  Commit: 9501b29d0dacc8b8efb18ae01733b2768dd40b62
  */
  int optimizehuffmanheader;

  /*
  Used to stop working on a block if there is specified amount of iterations
  without further bit reductions. Number of iterations should be greater
  than this value, otherwise it will have no effect.
  */
  int maxfailiterations;

  /*
  This has an impact on block splitting model by recursively checking multiple
  split points. Higher values slow down block splitting. Default is 9.
  */
  unsigned int findminimumrec;

  /*
  Initial randomness for iterations.
  Changing the default 1 and 2 allows zopfli to act more random
  on each run.
  */
  unsigned short ranstatew;
  unsigned short ranstatez;

  /*
  If to use Brotli RLE.
  */
  int usebrotli;

  /*
  Used to make reverse ordering of counts when weights are equal
  in bit length calculations as per GCC 5.3 defaults. Provides
  different results on block split points as well as iteration
  progress.
  */
  int revcounts;

  /*
  Recompress the file this many times after splitting last, it will
  run this many times ONLY if last block splitting is still smaller.
  */
  int pass;

  /*
  Zopfli restore points support by dumping important ZopfliDeflatePart
  variables and restoring them on next run. They are deleted everytime
  mentioned function finishes work so only useful when running expensive
  Zopfli Compression within ZopfliPNG only once / process.
  */
  int restorepoints;

  /*
  Disables block splitting last after compression.
  */
  int noblocksplittinglast;

  /*
  Tries 16 cominations of brotli, ohh, lazy and rc per block and
  picks smallest block size. This doesn't impact block splitting
  model. To make Zopfli calculate different block split points
  You need to additionally pass brotli, ohh, lazy and/or rc switches.
  */
  int tryall;

  /*
  Run expensive fixed calculations in block splitter. Slows down
  splitting A LOT, but will better find data that is good for fixed
  blocks compression.
  */
  int slowsplit;

};

// Returns 0 on success, error code otherwise.
// If verbose is true, it will print some info while working.
int ZopfliPNGOptimize(const std::vector<unsigned char>& origpng,
    const ZopfliPNGOptions& png_options,
    bool verbose,
    std::vector<unsigned char>* resultpng);

#endif  // __cplusplus

#endif  // ZOPFLIPNG_LIB_H_
