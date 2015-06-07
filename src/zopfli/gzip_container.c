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
*/

#include "gzip_container.h"
#include "util.h"
#include "crc32.h"

#include <stdio.h>

#include "deflate.h"

/*
Compresses the data according to the gzip specification.
*/
void ZopfliGzipCompress(const ZopfliOptions* options,
                        const unsigned char* in, size_t insize,
                        unsigned char** out, size_t* outsize, ZopfliAdditionalData* moredata) {
  unsigned long crcvalue = 0L;
  int i;
  const char* infilename = NULL;
  unsigned char bp=0;
  unsigned long fullsize;
  size_t rawdeflsize;
  size_t headersize=0;
  unsigned short havemoredata;
  if(moredata==NULL) {
    crcvalue = CRC(in, insize);
    fullsize = insize;
    havemoredata = 0;
    rawdeflsize = 0;
  } else {
    CRCu(in,insize,&moredata->checksum);
    crcvalue = moredata->checksum;
    infilename = moredata->filename;
    fullsize = moredata->fullsize;
    havemoredata = moredata->havemoredata;
    bp = moredata->bit_pointer;
    rawdeflsize = moredata->comp_size;
  }
  if(*outsize==0) {
  
    ZOPFLI_APPEND_DATA(31, out, outsize);  /* ID1 */
    ZOPFLI_APPEND_DATA(139, out, outsize);  /* ID2 */
    ZOPFLI_APPEND_DATA(8, out, outsize);  /* CM */
    if(infilename==NULL) {
      ZOPFLI_APPEND_DATA(0, out, outsize);  /* FLG */
    } else {
      ZOPFLI_APPEND_DATA(8, out, outsize);  /* FLG */
    }
  /* MTIME */
    if(moredata == NULL) {
      for(i=0;i<4;++i) ZOPFLI_APPEND_DATA(0, out, outsize);
    } else {
      for(i=0;i<4;++i) ZOPFLI_APPEND_DATA((moredata->timestamp >> (i*8)) % 256, out, outsize);
    }

    ZOPFLI_APPEND_DATA(2, out, outsize);  /* XFL, 2 indicates best compression. */
    ZOPFLI_APPEND_DATA(3, out, outsize);  /* OS follows Unix conventions. */
    headersize=10;
    if(infilename!=NULL) {
      for(i=0;infilename[i] != '\0';i++) {
        ++headersize;
        ZOPFLI_APPEND_DATA(infilename[i], out, outsize);
      }
      ++headersize;
      ZOPFLI_APPEND_DATA(0, out, outsize);
    }
  }

  if(fullsize<insize) fullsize=insize;

  ZopfliDeflate(options, 2 /* Dynamic block */, !havemoredata,
                in, insize, &bp, out, outsize, moredata);

  rawdeflsize+=(*outsize - headersize - havemoredata);
  if(moredata!=NULL) moredata->comp_size = rawdeflsize;

  if(havemoredata==0) {
  /* CRC */
    for(i=0;i<4;++i) ZOPFLI_APPEND_DATA((crcvalue >> (i*8)) % 256, out, outsize);

  /* ISIZE */
    for(i=0;i<4;++i) ZOPFLI_APPEND_DATA((fullsize >> (i*8)) % 256, out, outsize);

    if (options->verbose>1) {
      fprintf(stderr,
              "GZIP size: %d (%dK). Compression ratio: %.3f%%\n",
              (int)*outsize, (int)*outsize/1024,
              100.0 * (double)*outsize / (double)fullsize);
    }
  } else if(moredata!=NULL) {
    moredata->bit_pointer = bp;
  }
}
