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

#include "util.h"
#include "zip_container.h"
#include "crc32.h"

#include <stdio.h>

#include "deflate.h"

/*
Compresses the data according to the zip specification.
*/

void ZopfliZipCompress(const ZopfliOptions* options,
                        const unsigned char* in, size_t insize,
                        unsigned char** out, size_t* outsize, ZopfliAdditionalData* moredata) {
  unsigned long crcvalue = CRC(in, insize);
  unsigned long i;
  char* tempfilename = NULL;
  const char* infilename = NULL;
  unsigned long fullsize = insize & 0xFFFFFFFFUL;
  unsigned long rawdeflsize = 0;
  unsigned long headersize = 0;
  unsigned char bp = 0;
  size_t max = 0;
  unsigned char* cdirdata = NULL;
  unsigned long cdirsize = 0;
  unsigned long cdiroffset;
  if(moredata==NULL) {
    tempfilename = (char*)malloc(9 * sizeof(char*));
    sprintf(tempfilename,"%08lx",crcvalue & 0xFFFFFFFFUL);
    infilename = tempfilename;
  } else {
    infilename = moredata->filename;
  }

  if(*outsize==0) {

  /* File PK STATIC DATA */
    ZOPFLI_APPEND_DATA(80, out, outsize);
    ZOPFLI_APPEND_DATA(75, out, outsize);
    ZOPFLI_APPEND_DATA(3, out, outsize);
    ZOPFLI_APPEND_DATA(4, out, outsize);
    ZOPFLI_APPEND_DATA(20, out, outsize);
    ZOPFLI_APPEND_DATA(0, out, outsize);
    ZOPFLI_APPEND_DATA(2, out, outsize);
    ZOPFLI_APPEND_DATA(0, out, outsize);

  /* CM */
    ZOPFLI_APPEND_DATA(8, out, outsize);
    ZOPFLI_APPEND_DATA(0, out, outsize);

  /* MS-DOST TIME */
    if(moredata == NULL) {
      ZOPFLI_APPEND_DATA(32, out, outsize);
      for(i=0;i<3;++i) ZOPFLI_APPEND_DATA(0, out, outsize);
    } else {
      for(i=0;i<4;++i) ZOPFLI_APPEND_DATA((moredata->timestamp >> (i*8)) % 256, out, outsize);
    }

  /* CRC */
    for(i=0;i<4;++i) ZOPFLI_APPEND_DATA((crcvalue >> (i*8)) % 256, out, outsize);

  /* OSIZE NOT KNOWN YET - WILL UPDATE AFTER COMPRESSION */
    for(i=0;i<4;++i) ZOPFLI_APPEND_DATA(0, out, outsize);

  /* ISIZE */
    if(fullsize<insize) fullsize=insize;
    for(i=0;i<4;++i) ZOPFLI_APPEND_DATA((fullsize >> (i*8)) % 256, out, outsize);

  /* FNLENGTH */
    for(max=0;infilename[max] != '\0';max++) {}
    for(i=0;i<2;++i) ZOPFLI_APPEND_DATA((max >> (i*8)) % 256, out, outsize);

  /* NO EXTRA FLAGS */
    for(i=0;i<2;++i) ZOPFLI_APPEND_DATA(0, out, outsize);

  /* FILENAME */
    for(i=0;i<max;++i) ZOPFLI_APPEND_DATA(infilename[i], out, outsize);
    headersize = *outsize;
  }

  if(fullsize<insize) fullsize=insize;
  ZopfliDeflate(options, 2 /* Dynamic block */, 1,
                in, insize, &bp, out, outsize);

  rawdeflsize+=(*outsize - headersize);

  /* C-DIR PK STATIC DATA */
    ZOPFLI_APPEND_DATA(80,&cdirdata,&cdirsize);
    ZOPFLI_APPEND_DATA(75,&cdirdata,&cdirsize);
    for(i=1;i<3;++i) ZOPFLI_APPEND_DATA(i,&cdirdata,&cdirsize);
    for(i=0;i<2;++i) {
      ZOPFLI_APPEND_DATA(20,&cdirdata,&cdirsize);
      ZOPFLI_APPEND_DATA(0,&cdirdata,&cdirsize);
    }
    ZOPFLI_APPEND_DATA(2,&cdirdata,&cdirsize);
    ZOPFLI_APPEND_DATA(0,&cdirdata,&cdirsize);
    ZOPFLI_APPEND_DATA(8,&cdirdata,&cdirsize);
    ZOPFLI_APPEND_DATA(0,&cdirdata,&cdirsize);

 /* MS-DOS TIME, CRC, OSIZE, ISIZE FROM */

    if(moredata == NULL) {
      ZOPFLI_APPEND_DATA(32,&cdirdata,&cdirsize);
      for(i=0;i<3;++i) ZOPFLI_APPEND_DATA(0,&cdirdata,&cdirsize);
    } else {
      for(i=0;i<4;++i) ZOPFLI_APPEND_DATA((moredata->timestamp >> (i*8)) % 256,&cdirdata,&cdirsize);
    }
 /* CRC */
    for(i=0;i<4;++i) ZOPFLI_APPEND_DATA((crcvalue >> (i*8)) % 256,&cdirdata,&cdirsize);

 /* OSIZE + UPDATE IN PK HEADER */
    for(i=0;i<4;++i) ZOPFLI_APPEND_DATA((rawdeflsize >> (i*8)) % 256,&cdirdata,&cdirsize);
    for(i=0;i<4;++i) (*out)[18+i]=(rawdeflsize >> (i*8)) % 256;

 /* ISIZE */
    for(i=0;i<25;i+=8) ZOPFLI_APPEND_DATA((fullsize >> i) % 256,&cdirdata,&cdirsize);

 /* FILENAME LENGTH */
    for(max=0;infilename[max] != '\0';max++) {}
    for(i=0;i<2;++i) ZOPFLI_APPEND_DATA((max >> (i*8)) % 256,&cdirdata,&cdirsize);

 /* C-DIR STATIC DATA */
    for(i=0;i<8;++i) ZOPFLI_APPEND_DATA(0,&cdirdata,&cdirsize);
    ZOPFLI_APPEND_DATA(32,&cdirdata,&cdirsize);
    for(i=0;i<3;++i) ZOPFLI_APPEND_DATA(0,&cdirdata,&cdirsize);


 /* FilePK offset in ZIP file */
    for(i=0;i<4;++i) ZOPFLI_APPEND_DATA(0,&cdirdata,&cdirsize);
    cdiroffset=(unsigned long)(rawdeflsize+30+max);

 /* FILENAME */
    for(i=0; i<max;++i) ZOPFLI_APPEND_DATA(infilename[i],&cdirdata,&cdirsize);

    for(i=0; i<cdirsize; ++i) ZOPFLI_APPEND_DATA(cdirdata[i], out, outsize);

 /* END C-DIR PK STATIC DATA */
    ZOPFLI_APPEND_DATA(80,out,outsize);
    ZOPFLI_APPEND_DATA(75,out,outsize);
    ZOPFLI_APPEND_DATA(5,out,outsize);
    ZOPFLI_APPEND_DATA(6,out,outsize);
    for(i=4;i<8;++i) ZOPFLI_APPEND_DATA(0,out,outsize);

 /* TOTAL FILES IN ARCHIVE */
    
    for(i=0;i<2;++i) {
      ZOPFLI_APPEND_DATA(1,out,outsize);
      ZOPFLI_APPEND_DATA(0,out,outsize);
    }

 /* C-DIR SIZE */
    for(i=0;i<4;++i) ZOPFLI_APPEND_DATA((cdirsize >> (i*8)) % 256,out, outsize);

 /* C-DIR OFFSET */
    for(i=0;i<4;++i) ZOPFLI_APPEND_DATA((cdiroffset >> (i*8)) % 256,out, outsize);

 /* NO COMMENTS IN END C-DIR */
    for(i=20;i<22;++i) ZOPFLI_APPEND_DATA(0, out, outsize);

    if (options->verbose>1) {
      max=(cdiroffset+cdirsize)+22;
      fprintf(stderr,
              "ZIP size: %d (%dK). Compression ratio: %.3f%%\n",
              (int)max, (int)max / 1024,
              100.0 * (double)max / (double)fullsize);
    }

  free(cdirdata);
  free(tempfilename);
}
