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

#include <signal.h>
#include <dirent.h>
#include <sys/stat.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
/* Windows workaround for stdout output. */
#if _WIN32
#include <fcntl.h>
#endif

#include "inthandler.h"
#include "deflate.h"
#include "gzip_container.h"
#include "zip_container.h"
#include "zlib_container.h"

/*
Loads a file into a memory array.
*/

int mui;

void intHandler(int exit_code);

static void LoadFile(const char* filename,
                     unsigned char** out, size_t* outsize) {
  FILE* file;

  *out = 0;
  *outsize = 0;
  file = fopen(filename, "rb");
  if (!file) return;

  fseek(file , 0 , SEEK_END);
  *outsize = ftell(file);
  rewind(file);

  *out = (unsigned char*)malloc(*outsize);

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
  fseek(file,fseekdata,SEEK_SET);
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

static void CompressMultiFile(const ZopfliOptions* options,
                         const char* infilename,
                         const char* outfilename) {
  ZipCDIR zipcdir;
  char** filesindir = NULL;
  char* dirname = 0;
  char* fileindir = 0;
  unsigned char* in;
  size_t insize;
  unsigned char* out = 0;
  size_t outsize;
  size_t outsizeraw;
  size_t fseekdata = 0;
  unsigned int i;
  unsigned int j = 0;

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
    outsizeraw=0;
    fileindir=AddStrings(dirname,filesindir[i]);
    LoadFile(fileindir, &in, &insize);
    if(options->verbose>2) fprintf(stderr, "\n");
    if (insize == 0) {
      if(options->verbose>0) fprintf(stderr, "Invalid file: %s - trying next\n", fileindir);
    } else {
      if(options->verbose>0) fprintf(stderr, "[%d / %d] Adding file: %s\n", (i + 1), j, filesindir[i]);
      ZopfliZipCompress(options, in, insize, &out, &outsize, &outsizeraw, filesindir[i], &zipcdir);
      SaveFile(outfilename, out, outsize,fseekdata);
      fseekdata=zipcdir.offset;
      free(out);
      free(in);
    }
    free(filesindir[i]);
    free(fileindir);
  }
  free(filesindir);
  free(dirname);
  free(zipcdir.rootdir);
  free(zipcdir.data);
  free(zipcdir.enddata);
}

/*
outfilename: filename to write output to, or 0 to write to stdout instead
*/
static void CompressFile(const ZopfliOptions* options,
                         ZopfliFormat output_type,
                         const char* infilename,
                         const char* outfilename) {
  unsigned char* in;
  size_t insize;
  unsigned char* out = 0;
  size_t outsize = 0;
  size_t outsizeraw = 0;
  
  LoadFile(infilename, &in, &insize);
  if (insize == 0) {
    fprintf(stderr, "Invalid filename: %s\n", infilename);
    return;
  }

  ZopfliCompress(options, output_type, in, insize, &out, &outsize, &outsizeraw, infilename);
  if (outfilename) {
    SaveFile(outfilename, out, outsize,0);
  } else {
    size_t i;
    /* Windows workaround for stdout output. */
#if _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    for (i = 0; i < outsize; i++) {
      printf("%c", out[i]);
    }
#if _WIN32
    _setmode(_fileno(stdout), _O_TEXT);
#endif
  }

  free(out);
  free(in);
}

static void VersionInfo() {
  fprintf(stderr,
  "Zopfli, a Compression Algorithm to produce Deflate streams.\n"
  "KrzYmod extends Zopfli functionality - version 12\n\n");
}

int main(int argc, char* argv[]) {
  ZopfliOptions options;
  ZopfliFormat output_type = ZOPFLI_FORMAT_GZIP;
  const char* filename = 0;
  char* cbsbuff = NULL;
  int output_to_stdout = 0;
  int i, j;
  int k=1;

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
    else if (arg[0] == '-' && arg[1] == '-' && arg[2] == 'i'
        && arg[3] >= '0' && arg[3] <= '9') {
      options.numiterations = atoi(arg + 3);
    }  else if (arg[0] == '-' && arg[1] == '-' && arg[2] == 'm' && arg[3] == 'b'
        && arg[5] >= '0' && arg[5] <= '9') {
      options.blocksplittingmax = atoi(arg + 5);
    }  else if (arg[0] == '-' && arg[1] == '-' && arg[2] == 'm' && arg[3] == 'l' && arg[4] == 's'
        && arg[5] >= '0' && arg[5] <= '9') {
      options.lengthscoremax = atoi(arg + 5);
    }  else if (arg[0] == '-' && arg[1] == '-' && arg[2] == 'b' && arg[3] == 's' && arg[4] == 'r'
        && arg[5] >= '0' && arg[5] <= '9') {
      options.findminimumrec = atoi(arg + 5);
    }  else if (arg[0] == '-' && arg[1] == '-' && arg[2] == 'm' && arg[3] == 'u' && arg[4] == 'i'
        && arg[5] >= '0' && arg[5] <= '9') {
      options.maxfailiterations = atoi(arg + 5);
    }  else if (arg[0] == '-' && arg[1] == '-' && arg[2] == 'c' && arg[3] == 'b' && arg[4] == 's' && arg[5] != '\0') {
      cbsbuff = realloc(cbsbuff,2 * sizeof(char*));
      options.custblocksplit = realloc(options.custblocksplit, ++k * sizeof(unsigned long*));
      options.custblocksplit[0] = 1;
      options.custblocksplit[1] = 0;
      for(j=5;arg[j]!='\0';j++) {
        if(arg[j]!=',') {
          cbsbuff[0]=arg[j];
          cbsbuff[1]='\0';
          options.custblocksplit[k-1] = (options.custblocksplit[k-1]<<4) + strtoul(cbsbuff,NULL,16);
        } else {
          ++options.custblocksplit[0];
          options.custblocksplit = realloc(options.custblocksplit, ++k * sizeof(unsigned long*));
          options.custblocksplit[k-1] = 0;
        }
      }
      free(cbsbuff);
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
          "  --cbs#        custom block split points in hex separated with comma\n\n");
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
          "  --ohh         optymize huffman header (d: OFF)\n\n"
          " Pressing CTRL+C will set maximum unsuccessful iterations to 1.\n"
          "\n");
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

  if (options.maxfailiterations < 0) {
    fprintf(stderr, "Error: --mui parameter must be at least 0.\n");
    return 0;
  }

  if (options.findminimumrec < 2) {
    fprintf(stderr, "Error: --bsr parameter must be at least 2.\n");
    return 0;
  }

  mui = options.maxfailiterations;

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
            if(options.custblocksplit != NULL) {
              fprintf(stderr, "Error: --cbs works only in single file compression (no --dir).\n");
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
