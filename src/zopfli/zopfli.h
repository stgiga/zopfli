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
#include <stdlib.h> /* for size_t */

#ifdef __cplusplus
extern "C" {
#endif

/*
Options used throughout the program.
*/
typedef struct ZopfliOptions {
  /* How much output to print, verbose level */
  int verbose;

  /*
  Maximum amount of times to rerun forward and backward pass to optimize LZ77
  compression cost. Good values: 10, 15 for small files, 5 for files over
  several MB in size or it will be too slow.
  */
  int numiterations;

  /*
  If true, splits the data in multiple deflate blocks with optimal choice
  for the block boundaries. Block splitting gives better compression. Default:
  true (1).
  */
  int blocksplitting;

  /*
  If true, chooses the optimal block split points only after doing the iterative
  LZ77 compression. If false, chooses the block split points first, then does
  iterative LZ77 on each individual block. Depending on the file, either first
  or last gives the best compression. Default: false (0).
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
  Enable Lazy matching in LZ77Greedy, may provide different result.
  During my tests one file got reduced by 1 byte using this method.
  */
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
  Use scandir to get list of files to compress to ZIP. File will be updated
  on-fly after every file successfully gets compressed. So it should be 
  posible to copy it at any time while holding already successfully compressed files,
  or break the operation with CTRL+C and resume it later by manually getting rid
  of already compressed files in pointed directory.
  */
  int usescandir;

  /*
  This has an impact on block splitting model by recursively checking multiple
  split points. Higher values slow down block splitting. Default is 9.
  */
  unsigned int findminimumrec;

  /*
  Allows to set custom block size. This uses simple block splitting instead
  of zopfli auto guessing.
  */
  unsigned int blocksize;

  /*
  Allows to set custom number of blocks. This uses simple block splitting instead
  of zopfli auto guessing.
  */
  unsigned int numblocks;

  /*
  Custom block start points in hexadecimal format comma separated.
  */
  unsigned long *custblocksplit;

  /*
  Block types 0-2, comma separated
  */
  unsigned short *custblocktypes;

  /*
  Runs zopfli splitting between manual/custom start points
  */
  int additionalautosplits;

  /*
  Initial randomness for iterations.
  Changing the default 1 and 2 allows zopfli to act more random
  on each run.
  */
  unsigned short ranstatew;
  unsigned short ranstatez;

  /*
  Save block splits to file and exit zopfli
  */
  const char* dumpsplitsfile;
} ZopfliOptions;

/* Initializes options with default values. */
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
void ZopfliCompress(const ZopfliOptions* options, ZopfliFormat output_type,
                    const unsigned char* in, size_t insize,
                    unsigned char** out, size_t* outsize, size_t* outsizeraw, const char* infilename);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  /* ZOPFLI_ZOPFLI_H_ */
