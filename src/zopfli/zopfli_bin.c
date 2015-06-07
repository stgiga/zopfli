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

/*
Zopfli compressor program. It can output gzip-, zlib- or deflate-compatible
data. By default it creates a .gz file. This tool can only compress, not
decompress. Decompression can be done by any standard gzip, zlib or deflate
decompressor.
*/

#define _XOPEN_SOURCE 500
#define _FILE_OFFSET_BITS 64

#include <errno.h>
#include <signal.h>
#include <dirent.h>
#include <sys/stat.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
/* Windows workaround for stdout output. */
#if _WIN32
#include <fcntl.h>
#endif

#include "util.h"
#include "inthandler.h"
#include "deflate.h"
#include "gzip_container.h"
#include "zip_container.h"
#include "zlib_container.h"

/*
Loads a file into a memory array.
*/

void intHandler(int exit_code);

static int exists(const char* file) {
  FILE* ofile;
  if((ofile = fopen(file, "r")) != NULL) {
    fclose(ofile);
    return 1;
  }
  return 0;
}

static void LoadFile(const char* filename,
                     unsigned char** out, size_t* outsize, size_t* offset, size_t* filesize) {
  FILE* file;

  *out = 0;
  *outsize = 0;
  file = fopen(filename, "rb");
  if (!file) return;

  if(*offset==0) {
    unsigned char testfile;
    fseeko(file,(size_t)(-1), SEEK_SET);
    *filesize = ftello(file);
    if(*filesize > 0 && (fread(&testfile, 1, 1, file))==1) {
      fprintf(stderr,"Error: Files larger than %luMB are not supported by this version.\n",(unsigned long)((size_t)(-1)/1024/1024));
      exit(EXIT_FAILURE);
    }
  }
  fseeko(file , 0 , SEEK_END);
  *filesize = ftello(file);
  *outsize = *filesize-*offset;
  if(*outsize > ZOPFLI_MASTER_BLOCK_SIZE && ZOPFLI_MASTER_BLOCK_SIZE>0) {
    *outsize = ZOPFLI_MASTER_BLOCK_SIZE;
  }
  fseeko(file, *offset, SEEK_SET);
  *out = (unsigned char*)malloc(*outsize);
  *offset+=*outsize;

  if (*outsize && (*out)) {
    size_t testsize = fread(*out, 1, *outsize, file);
    if (testsize != *outsize) {
      /* It could be a directory */
      free(*out);
      *out = 0;
      *outsize = 0;
    }
  }
  assert(!(*outsize) || out);  /* If size is not zero, out must be allocated. */
  fclose(file);
}

/*
Saves a file from a memory array, overwriting the file if it existed.
*/
static void SaveFile(const char* filename,
                     const unsigned char* in, size_t insize, size_t fseekdata) {
  FILE* file;
  if(fseekdata==0) {
    file = fopen(filename, "wb");
  } else {
    file = fopen(filename, "r+b");
  }
  if(file == NULL) {
    fprintf(stderr,"Error: Can't write to output file, terminating.\n");
    exit (EXIT_FAILURE);
  }
  assert(file);
  fseeko(file,fseekdata,SEEK_SET);
  fwrite((char*)in, 1, insize, file);
  fclose(file);
}

static char StringsEqual(const char* str1, const char* str2) {
  return strcmp(str1, str2) == 0;
}

/*
Add two strings together. Size does not matter. Result must be freed.
*/
static char* AddStrings(const char* str1, const char* str2) {
  int a,b;
  char* result;
  for(a=0;str1[a]!='\0';a++) {}
  for(b=0;str2[b]!='\0';b++) {}
  result = (char*)malloc((a+b) + 1);
  if (!result) exit(-1); /* Allocation failed. */
  strcpy(result, str1);
  strcat(result, str2);
  return result;
}

static int ListDir(const char* filename, char ***filesindir, unsigned int *j, int isroot) {
  DIR *dir;
  struct dirent *ent;
  struct stat attrib;
  char* initdir=AddStrings(filename,"/");
  char* statfile=NULL;
  unsigned int i, k, l;
  dir = opendir(filename);
  if(! dir) {
    free(initdir);
    return 0;
  } else {
    while(1) {
      ent = readdir(dir);
      if(! ent) break;
      if(!StringsEqual(ent->d_name,".") && !StringsEqual(ent->d_name,"..")) {
        statfile=AddStrings(initdir,ent->d_name);
        stat(statfile, &attrib);
        free(statfile);
        if((attrib.st_mode & S_IFDIR)==0) {
          *filesindir = realloc(*filesindir,((unsigned int)*j+1)*(sizeof(char*)));
          if(isroot==1) {
            for(i=0;ent->d_name[i]!='\0';i++) {}
            (*filesindir)[*j] = malloc(i * sizeof(char*) +1);
            strcpy((*filesindir)[*j], ent->d_name);
          } else {
            for(i=0;initdir[i]!='/';i++) {}
            ++i;
            for(k=i;initdir[k]!='\0';k++) {}
            k-=i;
            for(l=0;ent->d_name[l]!='\0';l++) {}
            (*filesindir)[*j] = malloc(k+l * sizeof(char*)+1);
            statfile=AddStrings(initdir+i,ent->d_name);
            strcpy((*filesindir)[*j], statfile);
            free(statfile);
          }
          ++*j;
        } else {
          statfile=AddStrings(initdir,ent->d_name);
          ListDir(statfile, filesindir, j,0);
          free(statfile);
        }
      }
    }
    closedir(dir);
  }
  free(initdir);
  return 1;
}

static unsigned long GzipTimestamp(const char* file) {
  struct tm* tt;
  struct stat attrib;
  stat(file, &attrib);
  tt = gmtime(&(attrib.st_mtime));
  if(tt->tm_year<70) {
    tt->tm_year=70;
    mktime(tt);
  }
  return tt->tm_sec + tt->tm_min*60 + tt->tm_hour*3600 + tt->tm_yday*86400 +
          (tt->tm_year-70)*31536000 + ((tt->tm_year-69)/4)*86400 -
          ((tt->tm_year-1)/100)*86400 + ((tt->tm_year+299)/400)*86400;
}

static unsigned long ZipTimestamp(const char* file) {
  struct tm* tt;
  struct stat attrib;
  stat(file, &attrib);
  tt = localtime(&(attrib.st_mtime));
  if(tt->tm_year<80) {
    tt->tm_year=80;
    mktime(tt);
  }
  return ((tt->tm_year-80) << 25) + ((tt->tm_mon+1) << 21) + (tt->tm_mday << 16) +
         (tt->tm_hour << 11) + (tt->tm_min << 5) + (tt->tm_sec >> 1);
}

static void RemoveIfExists(const char* tempfilename, const char* outfilename) {
  int input;
  if(exists(outfilename)) {
    char answer = 0;
    fprintf(stderr,"\nFile %s already exists, overwrite? (y/N) ",outfilename);
    while((input = getchar())) {
      if (input == '\n' || input == EOF) {
        break;
      } else if(!answer) {
        answer = input;
      }
    }
    if(answer == 'y' || answer == 'Y') {
      if(remove(outfilename)!=0) fprintf(stderr,"Error: %s\n",strerror(errno));
    } else {
      fprintf(stderr,"Info: File was not replaced and left as tempfile: %s\n",tempfilename);
      return;
    }
  }
  if(rename(tempfilename,outfilename)!=0) {
    fprintf(stderr,"Error: %s\n",strerror(errno));
  }
}

static void CompressMultiFile(ZopfliOptions* options,
                         const char* infilename,
                         const char* outfilename) {
  ZopfliAdditionalData moredata;
  unsigned char* buff = NULL;
  ZipCDIR zipcdir;
  char* tempfilename;
  char** filesindir = NULL;
  char* dirname = 0;
  char* fileindir = 0;
  unsigned char* in;
  size_t insize;
  unsigned char* out = 0;
  unsigned char tempbyte;
  size_t outsize;
  size_t fseekdata = 0;
  size_t loffset = 0;
  size_t soffset;
  size_t pkoffset = 14;
  unsigned int i;
  unsigned short k;
  unsigned int j = 0;

  tempfilename = AddStrings(outfilename,".zopfli");

  if(ListDir(infilename, &filesindir, &j, 1)==0) {
    fprintf(stderr, "Error: %s is not a directory or doesn't exist.\n",infilename); 
    return;
  } else if(j==0) {
    fprintf(stderr, "Directory %s seems empty.\n", infilename);
    return;
  }
  InitCDIR(&zipcdir);
  dirname=AddStrings(infilename, "/");
  for(i=0;dirname[i]!='\0';i++) {}
  zipcdir.rootdir=realloc(zipcdir.rootdir,i*sizeof(char *)+1);
  memcpy(zipcdir.rootdir,dirname,i*sizeof(char *)+1);
  for(i = 0; i < j; ++i) {
    outsize=0;
    loffset=0;
    soffset=fseekdata;
    moredata.checksum=0L;
    moredata.processed = 0;
    moredata.timestamp = 0;
    moredata.bit_pointer = 0;
    moredata.comp_size = 0;
    fileindir=AddStrings(dirname,filesindir[i]);
    if(options->verbose>2) fprintf(stderr, "\n");
    if(options->verbose>0) fprintf(stderr, "[%d / %d] Adding file: %s\n", (i + 1), j, filesindir[i]);
    do {
      LoadFile(fileindir, &in, &insize, &loffset, &moredata.fullsize);
      if (moredata.fullsize == 0) {
        if(options->verbose>0) fprintf(stderr, "Invalid file: %s - trying next\n", fileindir);
        break;
      } else {
        for(k = 0;fileindir[k]!='\0';k++) {}
        if(moredata.fullsize>(4294967295u-(k*2+98+fseekdata+zipcdir.size))) {
          fprintf(stderr,"Error: File %s may exceed ZIP limits of 4GB - trying next\n", fileindir);
          break;
        }
      }
      if(moredata.timestamp == 0) moredata.timestamp = ZipTimestamp(fileindir);
      if(loffset<moredata.fullsize) moredata.havemoredata = 1; else moredata.havemoredata = 0;
      moredata.filename = fileindir;
      ZopfliZipCompress(options, in, insize, &out, &outsize, &zipcdir, &moredata);
      free(in);
      SaveFile(tempfilename, out, outsize,soffset);
      soffset+=outsize-1;
      tempbyte=out[outsize-1];
      free(out);
      out = (unsigned char*)malloc(sizeof(unsigned char*));
      outsize = 1;
      out[0]=tempbyte;
    } while(loffset<moredata.fullsize);
    buff = (unsigned char*)malloc(8 * sizeof(unsigned char*));
    for(k=0;k<4;++k) {
      buff[k] = (moredata.checksum >> (k*8)) % 256;
      buff[k+4] = (moredata.comp_size >> (k*8)) % 256;
    }
    SaveFile(tempfilename, buff, 8,(fseekdata + pkoffset));
    free(buff);
    fseekdata=zipcdir.offset;
    free(out);
    free(filesindir[i]);
    free(fileindir);
  }
  RemoveIfExists(tempfilename,outfilename);
  free(tempfilename);
  free(filesindir);
  free(dirname);
  free(zipcdir.rootdir);
  free(zipcdir.data);
  free(zipcdir.enddata);
}

/*
outfilename: filename to write output to, or 0 to write to stdout instead
*/
static void CompressFile(ZopfliOptions* options,
                         ZopfliFormat output_type,
                         const char* infilename,
                         const char* outfilename) {
  ZopfliAdditionalData moredata;
  unsigned char* in = NULL;
  char* tempfilename = NULL;
  size_t insize;
  unsigned char* out = 0;
  unsigned char tempbyte;
  size_t outsize = 0;
  size_t loffset = 0;
  size_t soffset = 0;
  size_t pkoffset = 14;
  unsigned short k;

  if(outfilename) tempfilename = AddStrings(outfilename,".zopfli");

  if(output_type == ZOPFLI_FORMAT_ZLIB) {
    moredata.checksum = 1L;
  } else {
    moredata.checksum = 0L;
  }
  moredata.timestamp = 0;
  moredata.processed = 0;
  moredata.bit_pointer = 0;
  moredata.comp_size = 0;
  do {
    LoadFile(infilename, &in, &insize, &loffset, &moredata.fullsize);
    if (moredata.fullsize == 0) {
      fprintf(stderr, "Error: Invalid filename: %s\n", infilename);
      exit(EXIT_FAILURE);
    } else {
      for(k = 0;infilename[k]!='\0';k++) {}
      if(output_type == ZOPFLI_FORMAT_ZIP && moredata.fullsize>(4294967295u-(k*2+98))) {
        fprintf(stderr,"Error: File %s may exceed ZIP limits of 4G\n", infilename);
        exit(EXIT_FAILURE);
      }
      if(moredata.timestamp == 0) {
        if(output_type == ZOPFLI_FORMAT_ZIP) {
          moredata.timestamp = ZipTimestamp(infilename);
        } else if(output_type == ZOPFLI_FORMAT_GZIP || output_type == ZOPFLI_FORMAT_GZIP_NAME) {
          moredata.timestamp = GzipTimestamp(infilename);
        } else {
          moredata.timestamp = 1;
        }
      }

      if(moredata.fullsize > ZOPFLI_MASTER_BLOCK_SIZE) {
        if(options->custblocksplit != NULL) {
          free(options->custblocksplit);
          options->custblocksplit = NULL;
        }
        if(options->custblocktypes != NULL) {
          free(options->custblocktypes);
          options->custblocktypes = NULL;
        }
        if(options->dumpsplitsfile != NULL) options->dumpsplitsfile = NULL;
      }
    }
    if(loffset<moredata.fullsize) moredata.havemoredata = 1; else moredata.havemoredata = 0;
    if(output_type == ZOPFLI_FORMAT_GZIP) {
      moredata.filename = NULL;
    } else {
      moredata.filename = infilename;
    }
    ZopfliCompress(options, output_type, in, insize, &out, &outsize, &moredata);
    free(in);
    if (!outfilename) {
      size_t i;
#if   _WIN32
      _setmode(_fileno(stdout), _O_BINARY);
#endif
      for (i = 0; i < (outsize-1); i++) printf("%c", out[i]);
    } else {
      SaveFile(tempfilename, out, outsize,soffset);
    }
      soffset+=outsize-1;
      tempbyte=out[outsize-1];
      free(out);
      out = (unsigned char*)malloc(sizeof(unsigned char*));
      outsize = 1;
      out[0]=tempbyte;
  } while(loffset<moredata.fullsize);
  if(output_type == ZOPFLI_FORMAT_ZIP) {
    unsigned char* buff = (unsigned char*)malloc(8 * sizeof(unsigned char*));
    for(k=0;k<4;++k) {
      buff[k] = (moredata.checksum >> (k*8)) % 256;
      buff[k+4] = (moredata.comp_size >> (k*8)) % 256;
    }
    SaveFile(tempfilename, buff, 8,pkoffset);
    free(buff);
  }

    if (!outfilename) {
      printf("%c", tempbyte);
#if   _WIN32
      _setmode(_fileno(stdout), _O_TEXT);
#endif
    } else {
      RemoveIfExists(tempfilename,outfilename);
      free(tempfilename);
    }
  free(out);
}

static void VersionInfo() {
  fprintf(stderr,
  "Zopfli, a Compression Algorithm to produce Deflate streams.\n"
  "KrzYmod extends Zopfli functionality - version TRUNK5\n\n");
}

static void ParseCustomBlockBoundaries(unsigned long** bs,unsigned short** bt, const char* data) {
  char buff[2] = {0, 0};
  int i = 0;
  size_t j, k=1;
  (*bs) = malloc(++k * sizeof(unsigned long*));
  (*bs)[0] = 1;
  (*bs)[1] = 0;
  (*bt) = malloc(k * sizeof(unsigned short*));
  (*bt)[0] = 1;
  (*bt)[1] = 2;
  for(j=0;data[j]!='\0';j++) {
    if(data[j]==',') {
      ++(*bs)[0];
      (*bs) = realloc((*bs), ++k * sizeof(unsigned long*));
      (*bs)[k-1] = 0;
      ++(*bt)[0];
      (*bt) = realloc((*bt),k * sizeof(unsigned short*));
      (*bt)[k-1] = 2;
    } else if(data[j]=='=') {
      i=1;
    } else {
      buff[0]=data[j];
      if(i==1) {
        (*bt)[k-1]=atoi(buff);
        if((*bt)[k-1]>2) (*bt)[k-1]=2;
        i=0;
      } else {
        (*bs)[k-1] = ((*bs)[k-1]<<4) + strtoul(buff,NULL,16);
      }
    }
  }
}

int main(int argc, char* argv[]) {
  ZopfliOptions options;
  ZopfliFormat output_type = ZOPFLI_FORMAT_GZIP;
  const char* filename = 0;
  int output_to_stdout = 0;
  int i;

  signal(SIGINT, intHandler);

  ZopfliInitOptions(&options);

  for (i = 1; i < argc; i++) {
    const char* arg = argv[i];
    if (StringsEqual(arg, "--c")) output_to_stdout = 1;
    else if (StringsEqual(arg, "--deflate")) output_type = ZOPFLI_FORMAT_DEFLATE;
    else if (StringsEqual(arg, "--zlib")) output_type = ZOPFLI_FORMAT_ZLIB;
    else if (StringsEqual(arg, "--gzip")) output_type = ZOPFLI_FORMAT_GZIP;
    else if (StringsEqual(arg, "--gzipname")) output_type = ZOPFLI_FORMAT_GZIP_NAME;
    else if (StringsEqual(arg, "--zip")) output_type = ZOPFLI_FORMAT_ZIP;
    else if (StringsEqual(arg, "--splitlast")) options.blocksplittinglast = 1;
    else if (StringsEqual(arg, "--lazy")) options.lazymatching = 1;
    else if (StringsEqual(arg, "--ohh")) options.optimizehuffmanheader = 1;
    else if (StringsEqual(arg, "--dir")) options.usescandir = 1;
    else if (StringsEqual(arg, "--aas")) options.additionalautosplits = 1;
    else if (arg[0] == '-' && arg[1] == '-' && arg[2] == 'i'
        && arg[3] >= '0' && arg[3] <= '9') {
      options.numiterations = atoi(arg + 3);
    }  else if (arg[0] == '-' && arg[1] == '-' && arg[2] == 'm' && arg[3] == 'b'
        && arg[4] >= '0' && arg[4] <= '9') {
      options.blocksplittingmax = atoi(arg + 4);
    }  else if (arg[0] == '-' && arg[1] == '-' && arg[2] == 'm' && arg[3] == 'l' && arg[4] == 's'
        && arg[5] >= '0' && arg[5] <= '9') {
      options.lengthscoremax = atoi(arg + 5);
    }  else if (arg[0] == '-' && arg[1] == '-' && arg[2] == 'b' && arg[3] == 's' && arg[4] == 'r'
        && arg[5] >= '0' && arg[5] <= '9') {
      options.findminimumrec = atoi(arg + 5);
    }  else if (arg[0] == '-' && arg[1] == '-' && arg[2] == 'r' && arg[3] == 'w'
        && arg[4] >= '0' && arg[4] <= '9') {
      options.ranstatew = atoi(arg + 4);
      if(options.ranstatew<1) options.ranstatew=1;
    }  else if (arg[0] == '-' && arg[1] == '-' && arg[2] == 'r' && arg[3] == 'z'
        && arg[4] >= '0' && arg[4] <= '9') {
      options.ranstatez = atoi(arg + 4);
      if(options.ranstatez<1) options.ranstatez=1;
    }  else if (arg[0] == '-' && arg[1] == '-' && arg[2] == 'm' && arg[3] == 'u' && arg[4] == 'i'
        && arg[5] >= '0' && arg[5] <= '9') {
      options.maxfailiterations = atoi(arg + 5);
    }  else if (arg[0] == '-' && arg[1] == '-' && arg[2] == 'c' && arg[3] == 'b' && arg[4] == 's' && arg[5] != '\0') {
      if(arg[5] == 'f' && arg[6] == 'i' && arg[7] == 'l' && arg[8] == 'e' && arg[9] != '\0') {
        const char *cbsfile = arg+9;
        FILE* file = fopen(cbsfile, "rb");
        char* filedata = NULL;
        size_t size;
        if(file==NULL) {
          fprintf(stderr,"Error: CBS file %s doesn't exist.\n",cbsfile);
          exit(EXIT_FAILURE);
        }
        fseeko(file,0,SEEK_END);
        size=ftello(file);
        if(size>0) {
          filedata = (char *) malloc((size+1) * sizeof(char*));
          rewind(file);
          if(fread(filedata,1,size,file)) {}
          filedata[size]='\0';
          ParseCustomBlockBoundaries(&options.custblocksplit,&options.custblocktypes,filedata);
          free(filedata);
        } else {
          fprintf(stderr,"Error: CBS file %s seems empty.\n",cbsfile);
          exit(EXIT_FAILURE);
        }
        fclose(file);
      } else {
        ParseCustomBlockBoundaries(&options.custblocksplit,&options.custblocktypes,arg+5);
      }
    }  else if (arg[0] == '-' && arg[1] == '-' && arg[2] == 'c' && arg[3] == 'b' && arg[4] == 'd' && arg[5] != '\0') {
       options.dumpsplitsfile = arg+5;
    }  else if (arg[0] == '-' && arg[1] == '-' && arg[2] == 'v' && arg[3] >= '0' && arg[3] <= '9') {
      options.verbose = atoi(arg + 3);
    }  else if (arg[0] == '-' && arg[1] == '-' && arg[2] == 'b' && arg[3] >= '0' && arg[3] <= '9') {
      options.blocksize = atoi(arg + 3);
    }  else if (arg[0] == '-' && arg[1] == '-' && arg[2] == 'n' && arg[3] >= '0' && arg[3] <= '9') {
      options.numblocks = atoi(arg + 3);
    }
    else if (arg[0] == '-' && (arg[1] == 'h' || arg[1] == '?' || (arg[1] == '-' && (arg[2] == 'h' || arg[2] == '?')))) {
      VersionInfo();
      fprintf(stderr,
          "Usage: zopfli [OPTIONS] FILE\n\n"
          "      GENERAL OPTIONS:\n"
          "  --h           shows this help (--?, -h, -?)\n"
          "  --v#          verbose level (0-5, d: 2)\n"
          "  --dir         accept directory as input, requires: --zip\n\n");
      fprintf(stderr,
          "      COMPRESSION TIME CONTROL:\n"
          "  --i#          perform # iterations (d: 15)\n"
          "  --mui#        maximum unsucessful iterations after last best (d: 0)\n\n");
      fprintf(stderr,
          "      AUTO BLOCK SPLITTING CONTROL:\n"
          "  --mb#         maximum blocks, 0 = unlimited (d: 15)\n"
          "  --bsr#        block splitting recursion (min: 2, d: 9)\n"
          "  --mls#        maximum length score (d: 1024)\n"
          "  --splitlast   do block splitting last instead of first\n\n");
      fprintf(stderr,
          "      MANUAL BLOCK SPLITTING CONTROL:\n"
          "  --n#          number of blocks\n"
          "  --b#          block size in bytes\n"
          "      THESE WILL NOT WORK ON FILES >%dMB:\n"
          "  --cbs#        customize block start points and types\n"
          "                format: hex_startb1[=type],hex_startb2[=type]\n"
          "                example: 0=0,33f0,56dd,8799=1,22220=0\n"
          "  --cbsfile#    same as above but instead read from file #\n"
          "  --cbd#        dump block start points to # file and exit\n"
          "  --aas         additional automatic splitting between manual points\n\n",(int)(ZOPFLI_MASTER_BLOCK_SIZE/1024/1024));
      fprintf(stderr,
          "      OUTPUT CONTROL:\n"
          "  --c           output to stdout\n"
          "  --zip         output to zip format\n"
          "  --gzip        output to gzip format (default)\n"
          "  --gzipname    output to gzip format with filename\n"
          "  --zlib        output to zlib format\n"
          "  --deflate     output to deflate format\n\n");
      fprintf(stderr,
          "      MISCELLANEOUS:\n"
          "  --lazy        lazy matching in Greedy LZ77 (d: OFF)\n"
          "  --ohh         optymize huffman header (d: OFF)\n"
          "  --rw#         initial random W for iterations (1-65535, d: 1)\n"
          "  --rz#         initial random Z for iterations (1-65535, d: 2)\n\n"
          " Pressing CTRL+C will set maximum unsuccessful iterations to 1.\n"
          "\n");
      fprintf(stderr,"Maximum supported input file size by this version is %luMB.\n\n",(unsigned long)((size_t)(-1)/1024/1024));
      return 0;
    }
  }

  if(options.verbose>0) VersionInfo();

  if (options.numiterations < 1) {
    fprintf(stderr, "Error: --i parameter must be at least 1.\n");
    return 0;
  }

  if (options.blocksplittingmax < 0) {
    fprintf(stderr, "Error: --mb parameter must be at least 0.\n");
    return 0;
  }

  if (options.lengthscoremax < 1) {
    fprintf(stderr, "Error: --mls parameter must be at least 1.\n");
    return 0;
  }

  if (options.findminimumrec < 2) {
    fprintf(stderr, "Error: --bsr parameter must be at least 2.\n");
    return 0;
  }

  for (i = 1; i < argc; i++) {
    if (argv[i][0] != '-') {
      char* outfilename;
      filename = argv[i];
      if (output_to_stdout) {
        outfilename = 0;
      } else if (output_type == ZOPFLI_FORMAT_GZIP || output_type == ZOPFLI_FORMAT_GZIP_NAME) {
        outfilename = AddStrings(filename, ".gz");
      } else if (output_type == ZOPFLI_FORMAT_ZLIB) {
        outfilename = AddStrings(filename, ".zlib");
      } else if (output_type == ZOPFLI_FORMAT_ZIP) {
        outfilename = AddStrings(filename, ".zip");
      } else {
        assert(output_type == ZOPFLI_FORMAT_DEFLATE);
        outfilename = AddStrings(filename, ".deflate");
      }
      if (options.verbose>0 && outfilename) {
        fprintf(stderr, "Saving to: %s\n\n", outfilename);
      }
      if(options.usescandir == 1) {
        if(output_type == ZOPFLI_FORMAT_ZIP && !output_to_stdout) {
            if(options.custblocksplit != NULL || options.dumpsplitsfile != NULL) {
              fprintf(stderr, "Error: --cbs and --cbd work only in single file compression (no --dir).\n");
              return 0;
            }
          CompressMultiFile(&options, filename, outfilename);
        } else {
          if(!output_to_stdout) {
            fprintf(stderr, "Error: --dir will only work with ZIP container (--zip).\n");
          } else {
            fprintf(stderr, "Error: Can't output to stdout when compressing multiple files (--dir and --c).\n");
          }
          return 0;
        }
      } else {
        CompressFile(&options, output_type, filename, outfilename);
      }
      free(outfilename);
    }
  }

  if (!filename) {
    fprintf(stderr,
            "Error: Please provide filename to compress.\nFor help, type: %s --h\n", argv[0]);
  }

  return 0;
}
