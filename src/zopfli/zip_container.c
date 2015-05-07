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
#include <string.h>

#include "deflate.h"

static unsigned long crc_table[256];

/* Flag: has the table been computed? Initially false. */
static int crc_table_computed = 0;

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
                        unsigned char** out, size_t* outsize, size_t* outsizeraw, const char *infilename) {
  unsigned long crcvalue = CRC(in, insize);
  unsigned long cdiroffset;
  unsigned long cdirlength = 46;
  unsigned msdos_date;
  unsigned msdos_time;
  unsigned char bp = 0;
  int i;
  int max = strlen(infilename);
  struct tm* tt;
  struct stat attrib;
  stat(infilename, &attrib);
  tt = localtime(&(attrib.st_mtime));
  if(tt->tm_year<80) {
    tt->tm_year=80;
    mktime(tt);
  }
  msdos_date = ((tt->tm_year-80) << 9) + ((tt->tm_mon+1) << 5) + tt->tm_mday;
  msdos_time = (tt->tm_hour << 11) + (tt->tm_min << 5) + (tt->tm_sec >> 1);

  /* File PK */
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

  /* OSIZE, need to get raw deflate stream size */
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

  cdirlength+=max;
  cdiroffset=*outsize;

  /* C-DIR PK */
  ZOPFLI_APPEND_DATA(80, out, outsize);
  ZOPFLI_APPEND_DATA(75, out, outsize);
  ZOPFLI_APPEND_DATA(1, out, outsize);
  ZOPFLI_APPEND_DATA(2, out, outsize);
  ZOPFLI_APPEND_DATA(20, out, outsize);
  ZOPFLI_APPEND_DATA(0, out, outsize);
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


  /* OSIZE, MODIFY ALSO IN File PK */
  ZOPFLI_APPEND_DATA(*outsizeraw % 256, out, outsize);
  ZOPFLI_APPEND_DATA((*outsizeraw >> 8) % 256, out, outsize);
  ZOPFLI_APPEND_DATA((*outsizeraw >> 16) % 256, out, outsize);
  ZOPFLI_APPEND_DATA((*outsizeraw >> 24) % 256, out, outsize);
  (*out)[18]=(*outsizeraw % 256);
  (*out)[19]=((*outsizeraw >> 8) % 256);
  (*out)[20]=((*outsizeraw >> 16) % 256);
  (*out)[21]=((*outsizeraw >> 24) % 256);

  /* ISIZE */
  ZOPFLI_APPEND_DATA(insize % 256, out, outsize);
  ZOPFLI_APPEND_DATA((insize >> 8) % 256, out, outsize);
  ZOPFLI_APPEND_DATA((insize >> 16) % 256, out, outsize);
  ZOPFLI_APPEND_DATA((insize >> 24) % 256, out, outsize);

  /* FNLENGTH */
  ZOPFLI_APPEND_DATA(max % 256, out, outsize);
  ZOPFLI_APPEND_DATA((max >> 8) % 256, out, outsize);

  /* SOME NOT NEEDED INFO */
  ZOPFLI_APPEND_DATA(0, out, outsize);
  ZOPFLI_APPEND_DATA(0, out, outsize);
  ZOPFLI_APPEND_DATA(0, out, outsize);
  ZOPFLI_APPEND_DATA(0, out, outsize);
  ZOPFLI_APPEND_DATA(0, out, outsize);
  ZOPFLI_APPEND_DATA(0, out, outsize);
  ZOPFLI_APPEND_DATA(0, out, outsize);
  ZOPFLI_APPEND_DATA(0, out, outsize);
  ZOPFLI_APPEND_DATA(32, out, outsize);
  ZOPFLI_APPEND_DATA(0, out, outsize);
  ZOPFLI_APPEND_DATA(0, out, outsize);
  ZOPFLI_APPEND_DATA(0, out, outsize);

  /* RELATIVE OFFSET, CURRENTLY ONLY ONE FILE SO 0 WILL DO */
  ZOPFLI_APPEND_DATA(0, out, outsize);
  ZOPFLI_APPEND_DATA(0, out, outsize);
  ZOPFLI_APPEND_DATA(0, out, outsize);
  ZOPFLI_APPEND_DATA(0, out, outsize);

  for(i=0;i<max;++i) ZOPFLI_APPEND_DATA(infilename[i], out, outsize);

  /* C-DIR END PK */

  ZOPFLI_APPEND_DATA(80, out, outsize);
  ZOPFLI_APPEND_DATA(75, out, outsize);
  ZOPFLI_APPEND_DATA(5, out, outsize);
  ZOPFLI_APPEND_DATA(6, out, outsize);
  ZOPFLI_APPEND_DATA(0, out, outsize);
  ZOPFLI_APPEND_DATA(0, out, outsize);
  ZOPFLI_APPEND_DATA(0, out, outsize);
  ZOPFLI_APPEND_DATA(0, out, outsize);

  /* NR OF ENTRIES, CURRENTLY ONLY 1 FILE */
  ZOPFLI_APPEND_DATA(1, out, outsize);
  ZOPFLI_APPEND_DATA(0, out, outsize);

  /* NR OF ENTRIES, CURRENTLY ONLY 1 FILE */
  ZOPFLI_APPEND_DATA(1, out, outsize);
  ZOPFLI_APPEND_DATA(0, out, outsize);

  /* C-DIR LENGTH */
  ZOPFLI_APPEND_DATA(cdirlength % 256, out, outsize);
  ZOPFLI_APPEND_DATA((cdirlength >> 8) % 256, out, outsize);
  ZOPFLI_APPEND_DATA((cdirlength >> 16) % 256, out, outsize);
  ZOPFLI_APPEND_DATA((cdirlength >> 24) % 256, out, outsize);

  /* C-DIR OFFSET */
  ZOPFLI_APPEND_DATA(cdiroffset % 256, out, outsize);
  ZOPFLI_APPEND_DATA((cdiroffset >> 8) % 256, out, outsize);
  ZOPFLI_APPEND_DATA((cdiroffset >> 16) % 256, out, outsize);
  ZOPFLI_APPEND_DATA((cdiroffset >> 24) % 256, out, outsize);

  /* 0 COMMENT LENGTH */
  ZOPFLI_APPEND_DATA(0, out, outsize);
  ZOPFLI_APPEND_DATA(0, out, outsize);

  if (options->verbose) {
    fprintf(stderr,
            "Original Size: %d, Zip: %d, Compression: %f%% Removed\n",
            (int)insize, (int)*outsize,
            100.0 * (double)(insize - *outsize) / (double)insize);
  }
}
