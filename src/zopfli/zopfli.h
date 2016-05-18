/*
Copyright 2011 Google Inc. All Rights Reserved.
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
*/

#ifndef ZOPFLI_ZOPFLI_H_
#define ZOPFLI_ZOPFLI_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
Options shared by both BIN and LIB.
*/
typedef struct ZopfliOptions {
  /* How much output to print, verbose level */
  int verbose;

  /*
  Maximum amount of times to rerun forward and backward pass to optimize LZ77
  compression cost. Good values: 10, 15 for small files, 5 for files over
  several MB in size or it will be too slow.
  */
  unsigned int numiterations;

  /*
  If true, splits the data in multiple deflate blocks with optimal choice
  for the block boundaries. Block splitting gives better compression. Default:
  true (1).
  */
  int blocksplitting;

  /*
  No longer used, left for compatibility.
  */
  int blocksplittinglast;

  /*
  Maximum amount of blocks to split into (0 for unlimited, but this can give
  extreme results that hurt compression on some files). Default value: 15.
  */
  int blocksplittingmax;

  /*
  Used to alter GetLengthScore max distance, this affects block splitting
  model and the chance for first run being closer to the optimal output.
  */
  int lengthscoremax;

  /*
  Used to stop working on a block if there is specified amount of iterations
  without further bit reductions. Number of iterations should be greater
  than this value, otherwise it will have no effect.
  */
  unsigned int maxfailiterations;

  /*
  This has an impact on block splitting model by recursively checking multiple
  split points. Higher values slow down block splitting. Default is 9.
  */
  unsigned int findminimumrec;

  /*
  Initial randomness for iterations.
  Changing the default 1 and 2 allows zopfli to act more random
  on each run. W using upper 16 bits, Z lower 16 bits.
  */
  unsigned long ranstatewz;

  /*
  Modulo used by random function. By default modulo 3 is used.
  Sometimes using different values (like 5) may give better results.
  */
  int ranstatemod;

  /*
  Recompress the file this many times after splitting last, it will
  run this many times ONLY if last block splitting is still smaller.
  */
  int pass;

  /*
  Zopfli restore points support by dumping important ZopfliDeflatePart
  variables and restoring them on next run.
  */
  int restorepoints;

  /*
  Disables block splitting last after compression, useful when
  custom block split points without changes are desired.
  */
  int noblocksplittinglast;

  /*
  Special compression mode as a set of bits:
  0000 - NONE,
  0001 - LAZY MATCHING,
  0010 - OPTIMIZE HUFFMAN HEADERS,
  0100 - REVERSE COUNTS (GCC 5.3 unstable qsort emulation),
  1000 - BROTLI RLE ENCODING
  */
  int mode;

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

  /*
  Iterate multiple dynamic blocks at once using pthreads, aka.
  multi-threading mode. Passing 0 forces compatibility behavior
  by running Block processing function with MASTER thread and
  displaying old fashioned statistics.
  */
  unsigned numthreads;

  /*
  If to use better random number generator by G. Marsaglia:
  "Complementary-Multiply-With-Carry".
  */
  int cmwc;

  /*
  Current stats to last stats importance in weighted statistic
  calculations. Default is 100, meaning 1 : 0.5.
  */
  int statimportance;

} ZopfliOptions;

/*
This struct holds 2 variables and is sent to LIB as last parameter
for ZIP or GZIP compression. Can be safely passed as NULL pointer.
*/
typedef struct ZopfliAdditionalData {
  /*
  Unix timestamp for GZIP or MS-DOS timestamp for ZIP. Will use
  lowest possible timestamp if structure is not passed.
  */
  unsigned long timestamp;

  /*
  Filename to store in archive. ZIP will use CRC32, GZIP will not
  store filename if structure is not passed.
  */
  const char* filename;

} ZopfliAdditionalData;

/*
This struct is used for custom block splits to be passed to LIB.
Can be safely passed as NULL pointer, otherwise must be in
read/write mode for ZopfliDeflatePart to update it with
best split point positions Zopfli considered the best.
*/
typedef struct ZopfliPredefinedSplits {
  /*
  Split points in uncompressed stream as byte offsets.
  */
  size_t* splitpoints;

  /*
  Amount of split points
  */
  size_t npoints;

  /*
  Tells ZopfliDeflatePart to try to further split input stream
  between defined split points.
  */
  int moresplitting;

} ZopfliPredefinedSplits;

/* Initializes shared options with default values. */
void ZopfliInitOptions(ZopfliOptions* options);

/* Output format */
typedef enum {
  ZOPFLI_FORMAT_GZIP,
  ZOPFLI_FORMAT_GZIP_NAME,
  ZOPFLI_FORMAT_ZLIB,
  ZOPFLI_FORMAT_DEFLATE,
  ZOPFLI_FORMAT_ZIP
} ZopfliFormat;

/*
Compresses according to the given output format and appends the result to the
output.

options: global program options
output_type: the output format to use
out: pointer to the dynamic output array to which the result is appended. Must
  be freed after use
outsize: pointer to the dynamic output array size
*/
void ZopfliCompress(ZopfliOptions* options, const ZopfliFormat output_type,
                    const unsigned char* in, size_t insize,
                    unsigned char** out, size_t* outsize, ZopfliPredefinedSplits* sp,
                    const ZopfliAdditionalData* moredata);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  /* ZOPFLI_H_ */
