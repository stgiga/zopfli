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

#include "defines.h"
#include "util.h"
#include "zopfli.h"

void ZopfliInitOptions(ZopfliOptions* options) {
  options->verbose = 2;
  options->numiterations = 15;
  options->blocksplitting = 1;
  options->blocksplittinglast = 0;
  options->blocksplittingmax = 15;
  options->lengthscoremax = 1024;
  options->lazymatching = 0;
  options->optimizehuffmanheader = 0;
  options->maxfailiterations = 0;
  options->findminimumrec = 9;
  options->ranstatew = 1;
  options->ranstatez = 2;
  options->usebrotli = 0;
  options->revcounts = 0;
  options->pass = 0;
  options->restorepoints = 0;
  options->noblocksplittinglast = 0;
  options->tryall = 0;
  options->slowsplit = 0;
  options->numthreads = 1;
  options->cmwc = 0;
  options->statimportance = 100;
}
