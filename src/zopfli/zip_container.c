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

#include <stdio.h>
#include <sys/stat.h>
#include <time.h>

#include "deflate.h"

static unsigned long crc_table[256];

/* Flag: has the table been computed? Initially false. */
static int crc_table_computed = 0;

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

static void MakeCRCTable() {
  unsigned long c;
  int n, k;
  for (n = 0; n < 256; n++) {
    c = (unsigned long) n;
    for (k = 0; k < 8; k++) {
      if (c & 1) {
        c = 0xedb88320L ^ (c >> 1);
      } else {
        c = c >> 1;
      }
    }
    crc_table[n] = c;
  }
  crc_table_computed = 1;
}


/*
Updates a running crc with the bytes buf[0..len-1] and returns
the updated crc. The crc should be initialized to zero.
*/
static unsigned long UpdateCRC(unsigned long crc,
                               const unsigned char *buf, size_t len) {
  unsigned long c = crc ^ 0xffffffffL;
  unsigned n;

  if (!crc_table_computed)
    MakeCRCTable();
  for (n = 0; n < len; n++) {
    c = crc_table[(c ^ buf[n]) & 0xff] ^ (c >> 8);
  }
  return c ^ 0xffffffffL;
}

/* Returns the CRC of the bytes buf[0..len-1]. */
static unsigned long CRC(const unsigned char* buf, int len) {
  return UpdateCRC(0L, buf, len);
}

/*
Compresses the data according to the zip specification.
*/

void ZopfliZipCompress(const ZopfliOptions* options,
                        const unsigned char* in, size_t insize,
                        unsigned char** out, size_t* outsize, size_t* outsizeraw, const char *infilename, ZipCDIR* zipcdir) {
  unsigned long crcvalue = CRC(in, insize);
  unsigned long oldcdirlength;
  unsigned msdos_date;
  unsigned msdos_time;
  unsigned char bp = 0;
  unsigned long i, j;
  unsigned long max;
  char* statfile;
  struct tm* tt;
  struct stat attrib;

  for(i=0;zipcdir->rootdir[i] != '\0';i++) {}
  for(max=0;infilename[max] != '\0';max++) {}

  statfile=malloc((i+max)*sizeof(char*)+1);

  statfile=zipcdir->rootdir;
  for(j=0;j<max;j++) {
    statfile[i++]=infilename[j];
  }

  oldcdirlength=zipcdir->size;
  zipcdir->size+=max+46;
  zipcdir->data=realloc(zipcdir->data,zipcdir->size*sizeof(unsigned char*));

  stat(statfile, &attrib);
  tt = localtime(&(attrib.st_mtime));
  if(tt->tm_year<80) {
    tt->tm_year=80;
    mktime(tt);
  }
  msdos_date = ((tt->tm_year-80) << 9) + ((tt->tm_mon+1) << 5) + tt->tm_mday;
  msdos_time = (tt->tm_hour << 11) + (tt->tm_min << 5) + (tt->tm_sec >> 1);

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

  /* MS-DOS TIME */
  ZOPFLI_APPEND_DATA(msdos_time % 256, out, outsize);
  ZOPFLI_APPEND_DATA((msdos_time >> 8) % 256, out, outsize);
  ZOPFLI_APPEND_DATA(msdos_date % 256, out, outsize);
  ZOPFLI_APPEND_DATA((msdos_date >> 8) % 256, out, outsize);

  /* CRC */
  ZOPFLI_APPEND_DATA(crcvalue % 256, out, outsize);
  ZOPFLI_APPEND_DATA((crcvalue >> 8) % 256, out, outsize);
  ZOPFLI_APPEND_DATA((crcvalue >> 16) % 256, out, outsize);
  ZOPFLI_APPEND_DATA((crcvalue >> 24) % 256, out, outsize);

  /* OSIZE UNKNOWN, UPDATE WHEN WRITING C-DIR */
  ZOPFLI_APPEND_DATA(0, out, outsize);
  ZOPFLI_APPEND_DATA(0, out, outsize);
  ZOPFLI_APPEND_DATA(0, out, outsize);
  ZOPFLI_APPEND_DATA(0, out, outsize);

  /* ISIZE */
  ZOPFLI_APPEND_DATA(insize % 256, out, outsize);
  ZOPFLI_APPEND_DATA((insize >> 8) % 256, out, outsize);
  ZOPFLI_APPEND_DATA((insize >> 16) % 256, out, outsize);
  ZOPFLI_APPEND_DATA((insize >> 24) % 256, out, outsize);

  /* FNLENGTH */
  ZOPFLI_APPEND_DATA(max % 256, out, outsize);
  ZOPFLI_APPEND_DATA((max >> 8) % 256, out, outsize);

  /* NO EXTRA FLAGS */
  ZOPFLI_APPEND_DATA(0, out, outsize);
  ZOPFLI_APPEND_DATA(0, out, outsize);

  /* FILENAME */
  for(i=0;i<max;++i) ZOPFLI_APPEND_DATA(infilename[i], out, outsize);

  ZopfliDeflate(options, 2 /* Dynamic block */, 1,
                in, insize, &bp, out, outsize);
  *outsizeraw=*outsize-max-30;

  /* C-DIR PK STATIC DATA */
  zipcdir->data[oldcdirlength++] = 80;
  zipcdir->data[oldcdirlength++] = 75;
  zipcdir->data[oldcdirlength++] = 1;
  zipcdir->data[oldcdirlength++] = 2;
  zipcdir->data[oldcdirlength++] = 20;
  zipcdir->data[oldcdirlength++] = 0;
  zipcdir->data[oldcdirlength++] = 20;
  zipcdir->data[oldcdirlength++] = 0;
  zipcdir->data[oldcdirlength++] = 2;
  zipcdir->data[oldcdirlength++] = 0;
  zipcdir->data[oldcdirlength++] = 8;
  zipcdir->data[oldcdirlength++] = 0;

  /* MS-DOS TIME */
  zipcdir->data[oldcdirlength++] = msdos_time % 256;
  zipcdir->data[oldcdirlength++] = (msdos_time >> 8) % 256;
  zipcdir->data[oldcdirlength++] = msdos_date % 256;
  zipcdir->data[oldcdirlength++] = (msdos_date >> 8) % 256;

  /* CRC */
  zipcdir->data[oldcdirlength++] = crcvalue % 256;
  zipcdir->data[oldcdirlength++] = (crcvalue >> 8) % 256;
  zipcdir->data[oldcdirlength++] = (crcvalue >> 16) % 256;
  zipcdir->data[oldcdirlength++] = (crcvalue >> 24) % 256;

  /* OSIZE */
  zipcdir->data[oldcdirlength++] = *outsizeraw % 256;
  zipcdir->data[oldcdirlength++] = (*outsizeraw >> 8) % 256;
  zipcdir->data[oldcdirlength++] = (*outsizeraw >> 16) % 256;
  zipcdir->data[oldcdirlength++] = (*outsizeraw >> 24) % 256;
  /* also update in File PK */
  (*out)[18]=(*outsizeraw % 256);
  (*out)[19]=((*outsizeraw >> 8) % 256);
  (*out)[20]=((*outsizeraw >> 16) % 256);
  (*out)[21]=((*outsizeraw >> 24) % 256);

  /* ISIZE */
  zipcdir->data[oldcdirlength++] = insize % 256;
  zipcdir->data[oldcdirlength++] = (insize >> 8) % 256;
  zipcdir->data[oldcdirlength++] = (insize >> 16) % 256;
  zipcdir->data[oldcdirlength++] = (insize >> 24) % 256;

  /* FILE NUMBER */
  zipcdir->data[oldcdirlength++] = max % 256;
  zipcdir->data[oldcdirlength++] = (max >> 8) % 256;

  /* C-DIR STATIC DATA */
  zipcdir->data[oldcdirlength++] = 0;
  zipcdir->data[oldcdirlength++] = 0;
  zipcdir->data[oldcdirlength++] = 0;
  zipcdir->data[oldcdirlength++] = 0;
  zipcdir->data[oldcdirlength++] = 0;
  zipcdir->data[oldcdirlength++] = 0;
  zipcdir->data[oldcdirlength++] = 0;
  zipcdir->data[oldcdirlength++] = 0;
  zipcdir->data[oldcdirlength++] = 32;
  zipcdir->data[oldcdirlength++] = 0;
  zipcdir->data[oldcdirlength++] = 0;
  zipcdir->data[oldcdirlength++] = 0;

  /* FilePK offset in ZIP file */
  zipcdir->data[oldcdirlength++] = zipcdir->offset % 256;
  zipcdir->data[oldcdirlength++] = (zipcdir->offset >> 8) % 256;
  zipcdir->data[oldcdirlength++] = (zipcdir->offset >> 16) % 256;
  zipcdir->data[oldcdirlength++] = (zipcdir->offset >> 24) % 256;

  /* FILENAME */
  for(i=0; i<max;++i) zipcdir->data[oldcdirlength++]=infilename[i];

  zipcdir->offset+=(unsigned long)*outsize;
  for(i=0; i<zipcdir->size; ++i) ZOPFLI_APPEND_DATA(zipcdir->data[i], out, outsize);

  ++zipcdir->fileid;

  /* END C-DIR PK STATIC DATA */
  zipcdir->enddata[0] = 80;
  zipcdir->enddata[1] = 75;
  zipcdir->enddata[2] = 5;
  zipcdir->enddata[3] = 6;
  zipcdir->enddata[4] = 0;
  zipcdir->enddata[5] = 0;
  zipcdir->enddata[6] = 0;
  zipcdir->enddata[7] = 0;

  /* TOTAL FILES IN ARCHIVE */
  zipcdir->enddata[8] = zipcdir->fileid % 256;
  zipcdir->enddata[9] = (zipcdir->fileid >> 8) % 256;
  zipcdir->enddata[10] = zipcdir->fileid % 256;
  zipcdir->enddata[11] = (zipcdir->fileid >> 8) % 256;

  /* C-DIR SIZE */
  zipcdir->enddata[12] = zipcdir->size % 256;
  zipcdir->enddata[13] = (zipcdir->size >> 8) % 256;
  zipcdir->enddata[14] = (zipcdir->size >> 16) % 256;
  zipcdir->enddata[15] = (zipcdir->size >> 24) % 256;

  /* C-DIR OFFSET */
  zipcdir->enddata[16] = zipcdir->offset % 256;
  zipcdir->enddata[17] = (zipcdir->offset >> 8) % 256;
  zipcdir->enddata[18] = (zipcdir->offset >> 16) % 256;
  zipcdir->enddata[19] = (zipcdir->offset >> 24) % 256;

  /* NO COMMENTS IN END C-DIR */
  zipcdir->enddata[20] = 0;
  zipcdir->enddata[21] = 0;

  for(i=0; i<22; ++i) ZOPFLI_APPEND_DATA(zipcdir->enddata[i], out, outsize);

  if (options->verbose) {
    max=(zipcdir->offset+zipcdir->size)+22;
    zipcdir->totalinput+=insize;
    fprintf(stderr,
            "Input Size: %d, Zip: %d, Compression: %f%% Removed\n",
            (int)zipcdir->totalinput, (int)max,
            100.0 * (double)(zipcdir->totalinput - max) / (double)zipcdir->totalinput);
  }
}
