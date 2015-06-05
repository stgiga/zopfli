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

#include "zopfli.h"

#include "deflate.h"
#include "gzip_container.h"
#include "zip_container.h"
#include "zlib_container.h"

void intHandler(int exit_code);

void ZopfliCompress(const ZopfliOptions* options, ZopfliFormat output_type,
                    const unsigned char* in, size_t insize, size_t fullsize, size_t* processed, unsigned char* bp,
                    unsigned char** out, size_t* outsize, size_t* outsizeraw, const char* infilename, unsigned long *checksum, unsigned char **adddata) {
  if (output_type == ZOPFLI_FORMAT_GZIP) {
    ZopfliGzipCompress(options, in, insize, fullsize, processed, bp, out, outsize, NULL, checksum, adddata);
  } else if (output_type == ZOPFLI_FORMAT_GZIP_NAME) {
    ZopfliGzipCompress(options, in, insize, fullsize, processed, bp, out, outsize, infilename, checksum, adddata);
  } else if (output_type == ZOPFLI_FORMAT_ZLIB) {
    ZopfliZlibCompress(options, in, insize, fullsize, processed, bp, out, outsize, checksum);
  } else if (output_type == ZOPFLI_FORMAT_ZIP) {
    ZipCDIR zipcdir;
    InitCDIR(&zipcdir);
    zipcdir.rootdir=realloc(zipcdir.rootdir,3*sizeof(char *));
    zipcdir.rootdir[0]='.';
    zipcdir.rootdir[1]='/';
    zipcdir.rootdir[2]='\0';
    ZopfliZipCompress(options, in, insize, fullsize, processed, bp, out, outsize, outsizeraw, infilename, checksum, &zipcdir, adddata);
    free(zipcdir.rootdir);
    free(zipcdir.data);
    free(zipcdir.enddata);
  } else if (output_type == ZOPFLI_FORMAT_DEFLATE) {
    if(fullsize<insize) fullsize=insize;
    ZopfliDeflate(options, 2 /* Dynamic block */, !options->havemoredata,
                  in, insize, bp, out, outsize, fullsize, processed);
  } else {
    exit (EXIT_FAILURE);
  }
}
