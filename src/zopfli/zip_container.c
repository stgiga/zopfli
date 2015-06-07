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

void InitCDIR(ZipCDIR *zipcdir) {
  zipcdir->rootdir = NULL;
  zipcdir->data = NULL;
  zipcdir->enddata = malloc(22);
  zipcdir->totalinput = 0;
  zipcdir->size = 0;
  zipcdir->curfileoffset = 0;
  zipcdir->offset = 0;
  zipcdir->fileid = 0;
}

/*
Compresses the data according to the zip specification.
*/

void ZopfliZipCompress(const ZopfliOptions* options,
                        const unsigned char* in, size_t insize,
                        unsigned char** out, size_t* outsize, ZipCDIR* zipcdir, ZopfliAdditionalData* moredata) {
  unsigned long crcvalue = 0L;
  unsigned long i;
  char* tempfilename = NULL;
  const char* infilename = NULL;
  unsigned char bp = 0;
  unsigned long fullsize;
  unsigned long rawdeflsize;
  unsigned long headersize = 0;
  unsigned short havemoredata;
  size_t max = 0;
  if(moredata==NULL) {
    tempfilename = (char*)malloc(9 * sizeof(char*));
    crcvalue = CRC(in, insize);
    sprintf(tempfilename,"%08lx",crcvalue & 0xFFFFFFFFUL);
    infilename = tempfilename;
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

  /* CRC AND OSIZE NOT KNOWN YET - UPDATE TO ADDDATA BUFFER */
    for(i=0;i<8;++i) ZOPFLI_APPEND_DATA(0, out, outsize);

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
  ZopfliDeflate(options, 2 /* Dynamic block */, !havemoredata,
                in, insize, &bp, out, outsize, moredata);

  rawdeflsize+=(*outsize - headersize - havemoredata);
  if(moredata!=NULL) moredata->comp_size = rawdeflsize;

  if(havemoredata==0) {

  /* C-DIR PK STATIC DATA */
    ZOPFLI_APPEND_DATA(80,&zipcdir->data,&zipcdir->size);
    ZOPFLI_APPEND_DATA(75,&zipcdir->data,&zipcdir->size);
    for(i=1;i<3;++i) ZOPFLI_APPEND_DATA(i,&zipcdir->data,&zipcdir->size);
    for(i=0;i<2;++i) {
      ZOPFLI_APPEND_DATA(20,&zipcdir->data,&zipcdir->size);
      ZOPFLI_APPEND_DATA(0,&zipcdir->data,&zipcdir->size);
    }
    ZOPFLI_APPEND_DATA(2,&zipcdir->data,&zipcdir->size);
    ZOPFLI_APPEND_DATA(0,&zipcdir->data,&zipcdir->size);
    ZOPFLI_APPEND_DATA(8,&zipcdir->data,&zipcdir->size);
    ZOPFLI_APPEND_DATA(0,&zipcdir->data,&zipcdir->size);

 /* MS-DOS TIME, CRC, OSIZE, ISIZE FROM */

    if(moredata == NULL) {
      ZOPFLI_APPEND_DATA(32,&zipcdir->data,&zipcdir->size);
      for(i=0;i<3;++i) ZOPFLI_APPEND_DATA(0,&zipcdir->data,&zipcdir->size);
    } else {
      for(i=0;i<4;++i) ZOPFLI_APPEND_DATA((moredata->timestamp >> (i*8)) % 256,&zipcdir->data,&zipcdir->size);
    }
    
    for(i=0;i<4;++i) ZOPFLI_APPEND_DATA((crcvalue >> (i*8)) % 256,&zipcdir->data,&zipcdir->size);
    for(i=0;i<4;++i) ZOPFLI_APPEND_DATA((rawdeflsize >> (i*8)) % 256,&zipcdir->data,&zipcdir->size);

    for(i=0;i<25;i+=8) ZOPFLI_APPEND_DATA((fullsize >> i) % 256,&zipcdir->data,&zipcdir->size);

  /* FILE NUMBER */
    for(max=0;infilename[max] != '\0';max++) {}
    for(i=0;i<2;++i) ZOPFLI_APPEND_DATA((max >> (i*8)) % 256,&zipcdir->data,&zipcdir->size);

  /* C-DIR STATIC DATA */
    for(i=0;i<8;++i) ZOPFLI_APPEND_DATA(0,&zipcdir->data,&zipcdir->size);
    ZOPFLI_APPEND_DATA(32,&zipcdir->data,&zipcdir->size);
    for(i=0;i<3;++i) ZOPFLI_APPEND_DATA(0,&zipcdir->data,&zipcdir->size);


  /* FilePK offset in ZIP file */
    for(i=0;i<4;++i) ZOPFLI_APPEND_DATA((zipcdir->offset >> (i*8)) % 256,&zipcdir->data,&zipcdir->size);
    zipcdir->offset+=(unsigned long)(rawdeflsize+30+max);
    fprintf(stderr,"Debug: %lu\n",zipcdir->offset);

  /* FILENAME */
    for(i=0; i<max;++i) ZOPFLI_APPEND_DATA(infilename[i],&zipcdir->data,&zipcdir->size);

    for(i=0; i<zipcdir->size; ++i) ZOPFLI_APPEND_DATA(zipcdir->data[i], out, outsize);

    ++zipcdir->fileid;

  /* END C-DIR PK STATIC DATA */
    zipcdir->enddata[0] = 80;
    zipcdir->enddata[1] = 75;
    zipcdir->enddata[2] = 5;
    zipcdir->enddata[3] = 6;
    for(i=4;i<8;++i) zipcdir->enddata[i] = 0;

  /* TOTAL FILES IN ARCHIVE */
    for(i=0;i<2;++i) {
      zipcdir->enddata[i+8] = (zipcdir->fileid >> (i*8)) % 256;
      zipcdir->enddata[i+10] = (zipcdir->fileid >> (i*8)) % 256;
    }

  /* C-DIR SIZE */
    for(i=0;i<4;++i) zipcdir->enddata[i+12] = (zipcdir->size >> (i*8)) % 256;

  /* C-DIR OFFSET */
    for(i=0;i<4;++i) zipcdir->enddata[i+16] = (zipcdir->offset >> (i*8)) % 256;

  /* NO COMMENTS IN END C-DIR */
    for(i=20;i<22;++i) zipcdir->enddata[i] = 0;

    for(i=0; i<22; ++i) ZOPFLI_APPEND_DATA(zipcdir->enddata[i], out, outsize);

    if (options->verbose>1) {
      max=(zipcdir->offset+zipcdir->size)+22;
      zipcdir->totalinput+=fullsize;
      fprintf(stderr,
              "ZIP size: %d (%dK). Compression ratio: %.3f%%\n",
              (int)max, (int)max / 1024,
              100.0 * (double)max / (double)zipcdir->totalinput);
    }
  } else if(moredata!=NULL) {
    moredata->bit_pointer = bp;
  }
  free(tempfilename);
}
