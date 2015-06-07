/*
  Simple libzopfli.dll test for proper compatibility of usage without bin part by Mr_KrzYch00.

  As You can see the difference between KrzYmod version and the original is that the original
  requires to pass ZopfliOptions structure while having a total of 6 parameters.

  KrzYmod on the other hand has 7 parameters but allows omitting ZopfliOptions and ZopfliAdditionalData
  structures making it requiring only 5 parameters that are simplier to pass.

  ZopfliDeflate and ZopfliDeflatePart are also accessible through lib call but they require
  ZopfliOptions structure to be passed and do not support max unsuccessful iterations after last best.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Used https://github.com/dlfcn-win32/dlfcn-win32 in order to work with mingw */
#include <dlfcn.h>

typedef struct ZopfliAdditionalData {
/* we don't really need this but we also don't want compilation warnings */
  int dummy;
} ZopfliAdditionalData;

typedef struct ZopfliOptions {
/* Options will be initialized before 2nd DLL call */
  int verbose;
  unsigned int numiterations;
  int blocksplitting;
  int blocksplittinglast;
  int blocksplittingmax;
  int lengthscoremax;
  int lazymatching;
  int optimizehuffmanheader;
  unsigned int maxfailiterations;
  int usescandir;
  unsigned int findminimumrec;
  unsigned int blocksize;
  unsigned int numblocks;
  unsigned long *custblocksplit;
  unsigned short *custblocktypes;
  int additionalautosplits;
  unsigned short ranstatew;
  unsigned short ranstatez;
  const char* dumpsplitsfile;
} ZopfliOptions;

void ZopfliInitOptions(ZopfliOptions* options) {
/* customize below to Your liking */
  options->verbose = 5;
  options->numiterations = 999999;
  options->blocksplitting = 1;
  options->blocksplittinglast = 0;
  options->blocksplittingmax = 15;
  options->lengthscoremax = 1024;
  options->lazymatching = 0;
  options->optimizehuffmanheader = 0;
  options->maxfailiterations = 999;
  options->usescandir = 0;
  options->findminimumrec = 9;
  options->blocksize = 0;
  options->numblocks = 0;
  options->custblocksplit = NULL;
  options->custblocktypes = NULL;
  options->additionalautosplits = 0;
  options->ranstatew = 1;
  options->ranstatez = 2;
  options->dumpsplitsfile = NULL;
}

int main(void) {

/* let's get some parameters ready */
  unsigned char* out = NULL; /* point to nothings since Zopfli DLL will take care of memory allocations */
  unsigned char* in = NULL; /* will be used to convert signed to unsigned char from stdin buffer */
  size_t outsize = 0; /* when out is NULL this MUST be 0 */
  size_t i, j; /* used for stdout dump and getting only what we really typed in stdin */
  ZopfliOptions options; /* to test 2nd time DLL is called in this example */

/* stdin buffer */
  char buff[1024];

  /* Zopfli DLL name */  
  char lib_file[14]    = {'l','i','b','z','o','p','f','l','i','.','d','l','l',0};
  /* Function name in Zopfli DLL to run */
  char func_to_run[15] = {'Z','o','p','f','l','i','C','o','m','p','r','e','s','s',0};

  /* Initialize DLL function call arguments */
  void (*funcp)(struct ZopfliOptions *options,unsigned short output_type,const unsigned char* in,size_t insize,unsigned char** out, size_t* outsize, struct ZopfliAdditionalData *moredata);
  /* Open DLL file */
  int *lib = dlopen(lib_file,RTLD_NOW);

  if(lib == NULL) {
    fprintf(stderr,"Error: Can't open lib: %s\n",lib_file);
    return EXIT_FAILURE;
  }

  /* Initialize function with ISO C warning fix */
  *(void **) (&funcp) = dlsym(lib,func_to_run);

  if(funcp == NULL) {
    fprintf(stderr,"Error: Can't open lib function: %s\n",func_to_run);
    return EXIT_FAILURE;
  }

  /* let's get some data from stdin */
  fprintf(stderr,"Type in something up to 1024 chars: ");
  fflush(stdout);
  fgets(buff,sizeof(buff),stdin);

  /* signed to unsigned to malloc'ed in buffer */
  for(j=0;buff[j]!='\n';j++) {}
  in=(unsigned char*)malloc(j * sizeof(unsigned char*) + 1);
  for(j=0;buff[j]!='\n';j++) {in[j] = *(unsigned char*)(&buff[j]);}

  /* Call DLL function with parameters while omitting options structure */
  fprintf(stderr,"\n\nRunning without options:\n");
  (*funcp)(NULL,2,in,j,&out,&outsize,NULL);

  /* dump ZLIB compression stread to stdout */
  for(i=0;i<outsize;i++) {
    printf("%c",out[i]);
  }

  /* clean output buffer for recompression */
  free(out);
  out = NULL;
  outsize = 0;

  /* Now initialize options structure and call DLL function again */
  fprintf(stderr,"\n\nRunning with options:\n");
  ZopfliInitOptions(&options);
  (*funcp)(&options,2,in,j,&out,&outsize,NULL);

  /* dump ZLIB compression stread to stdout */
  for(i=0;i<outsize;i++) {
    printf("%c",out[i]);
  }

  /* final cleaning */
  free(in);
  free(out);

  return 0;
}
