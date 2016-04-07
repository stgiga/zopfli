/*
Copyright 2016 Google Inc. All Rights Reserved.
Copyright 2016 Frédéric Kayser. All Rights Reserved.
Copyright 2016 Aaron Kaluszka. All Rights Reserved.
Copyright 2016 Mr_KrzYch00. All Rights Reserved.

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

Command line tool to recompress and optimize PNG images, using zopflipng_lib.
*/

#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
/* Windows priority setter. */
#if _WIN32
#include <windows.h>
static void IdlePriority() {
 if(SetPriorityClass(GetCurrentProcess(), IDLE_PRIORITY_CLASS)==0) {
  fprintf(stderr,"ERROR! Failed setting priority!\n\n");
 } else {
  fprintf(stderr,"INFO: Idle priority successfully set.\n\n");
 }
}
#else
#include <sys/resource.h>
static void IdlePriority() {
 if(setpriority(PRIO_PROCESS, 0, 19)==-1) {
  fprintf(stderr,"ERROR! Failed setting priority!\n\n");
 } else {
  fprintf(stderr,"INFO: Idle priority successfully set.\n\n");
 }
}
#endif

#include "lodepng/lodepng.h"
#include "zopflipng_lib.h"
#include "lodepng/lodepng_util.h"
#include "../zopfli/inthandler.h"

void intHandlerpng(int exit_code) {
  if(exit_code==2) {
    fprintf(stderr,"                                                              \n"
                   " (!!) CTRL+C detected! Setting --mui to 1 to finish work ASAP!\n"
                   " (!!) Press it again to abort work.\n");
    mui=1;
  }
}

// Returns directory path (including last slash) in dir, filename without
// extension in file, extension (including the dot) in ext
void GetFileNameParts(const std::string& filename,
    std::string* dir, std::string* file, std::string* ext) {
  size_t npos = (size_t)(-1);
  size_t slashpos = filename.find_last_of("/\\");
  std::string nodir;
  if (slashpos == npos) {
    *dir = "";
    nodir = filename;
  } else {
    *dir = filename.substr(0, slashpos + 1);
    nodir = filename.substr(slashpos + 1);
  }
  size_t dotpos = nodir.find_last_of('.');
  if (dotpos == (size_t)(-1)) {
    *file = nodir;
    *ext = "";
  } else {
    *file = nodir.substr(0, dotpos);
    *ext = nodir.substr(dotpos);
  }
}

// Returns whether the file exists and we have read permissions.
bool FileExists(const std::string& filename) {
  FILE* file = fopen(filename.c_str(), "rb");
  if (file) {
    fclose(file);
    return true;
  }
  return false;
}

// Returns the size of the file, if it exists and we have read permissions.
size_t GetFileSize(const std::string& filename) {
  size_t size;
  FILE* file = fopen(filename.c_str(), "rb");
  if (!file) return 0;
  fseek(file , 0 , SEEK_END);
  size = static_cast<size_t>(ftell(file));
  fclose(file);
  return size;
}

void ShowHelp() {
  printf("Usage: zopflipng [options]... infile.png outfile.png\n"
         "       zopflipng [options]... --prefix=[fileprefix] [files.png]...\n"
         "\n"
         "If the output file exists, it is considered a result from a"
         " previous run and not overwritten if its filesize is smaller.\n"
         "\n"
         "Options:\n"
         "-m: compress more: use more iterations (depending on file size)\n"
         "--prefix=[fileprefix]: Adds a prefix to output filenames. May also"
         " contain a directory path. When using a prefix, multiple input files"
         " can be given and the output filenames are generated with the"
         " prefix\n"
         " If --prefix is specified without value, 'zopfli_' is used.\n"
         " If input file names contain the prefix, they are not processed but"
         " considered as output from previous runs. This is handy when using"
         " *.png wildcard expansion with multiple runs.\n"
         "-y: do not ask about overwriting files.\n"
         "--alpha_cleaner=[0-5]: remove colors behind alpha channel 0. No"
         " visual difference, removes hidden information, based on cryopng.\n"
         "--lossy_transparent: backwards compat., same as alpha cleaner 1.\n"
         "--lossy_8bit: convert 16-bit per channel image to 8-bit per"
         " channel.\n"
         "-d: dry run: don't save any files, just see the console output"
         " (e.g. for benchmarking)\n"
         "--always_zopflify: always output the image encoded by Zopfli, even if"
         " it's bigger than the original, for benchmarking the algorithm. Not"
         " good for real optimization.\n"
         "-q: use quick, but not very good, compression"
         " (e.g. for only trying the PNG filter and color types)\n"
         "--iterations=[number]: number of iterations, more iterations makes it"
         " slower but provides slightly better compression. Default: 15 for"
         " small files, 5 for large files.\n"
         "--nosplittinglast: don't use last splitting after compression\n"
         "--filters=[types]: filter strategies to try:\n"
         " 0-4: give all scanlines PNG filter type 0-4\n"
         " m: minimum sum\n"
         " y: distinct bytes\n"
         " w: distinct byte pairs\n"
         " e: entropy\n"
         " b: brute force (slow)\n"
         " i: incremental brute force (very slow)\n"
         " p: predefined (keep from input, this likely overlaps another"
         " strategy)\n"
         " g: genetic algorithm*\n"
         " By default, if this argument is not given, all strategies are tried."
         "\n"
         " *Genetic algorithm filter options:\n"
         "  --ga_population_size: number of genomes in pool. Default: 19\n"
         "  --ga_max_evaluations: overall maximum number of evaluations (0 for"
         "   unlimited). Default: 0\n"
         "  --ga_stagnate_evaluations: number of sequential evaluations to try"
         "   without improvement. Default: 15\n"
         "  --ga_mutation_probability: probability of mutation per gene per"
         "   generation. Default: 0.01\n"
         "  --ga_crossover_probability: probability of crossover pergeneration."
         "   Default: 0.9\n"
         "  --ga_number_of_offspring: number of offspring per generation."
         "   Default: 2\n"
         "\n"
         "--zopfli_filters: by default, if this argument is not given, the"
         " filter that is most likely the best for this image is chosen by"
         " trying faster compression with each given type. If this argument is"
         " used, all given filter types are tried with slow compression and the"
         " best result retained.\n"
         "--keepchunks=nAME,nAME,...: keep metadata chunks with these names"
         " that would normally be removed, e.g. tEXt,zTXt,iTXt,gAMA, ... \n"
         " Due to adding extra data, this increases the result size. Keeping"
         " bKGD or sBIT chunks may cause additional worse compression due to"
         " forcing a certain color type, it is advised to not keep these for"
         " web images because web browsers do not use these chunks. By default,"
         " ZopfliPNG only keeps (and losslessly modifies) the following chunks"
         " because they are essential: IHDR, PLTE, tRNS, IDAT and IEND.\n\n"
         "     CUSTOM ZOPFLIPNG OPTIONS:\n"
         "--v=[number]:    verbose level for zopfli (0-5, d:2)\n"
         "--mui=[number]:  maximum unsuccessful iterations after last best (d: 0)\n"
         "--mb=[number]:   maximum blocks, 0 = unlimited (d: 15)\n"
         "--bsr=[number]:  block splitting recursion (min: 2, d: 9)\n"
         "--mls=[number]:  maximum length score (d: 1024)\n"
         "--slowsplit:     use expensive fixed block calculations\n"
         "--nosplitlast:   don't use last splitting after compression\n"
         "--all:           use 16 combinations per block and take smallest size\n"
         "--brotli:        use Brotli Huffman optimization\n"
         "--lazy:          lazy matching in Greedy LZ77\n"
         "--ohh:           optymize huffman header\n"
         "--rc:            reverse counts ordering in bit length calculations\n"
         "--pass=[number]: recompress last split points max # times (d: 0)\n"
         "--rp:            use restore points\n"
         "--rw=[number]:   initial random W for iterations (1-65535, d: 1)\n"
         "--rz=[number]:   initial random Z for iterations (1-65535, d: 2)\n"
         "--t=[number]:    compress using # threads, 0 = compat. (d:1)\n"
         "--idle           use idle process priority\n"
         "   more options available only in Zopfli\n"
         "\n"
         "Usage examples:\n"
         "Optimize a file and overwrite if smaller: zopflipng infile.png"
         " outfile.png\n"
         "Compress more: zopflipng -m infile.png outfile.png\n"
         "Optimize multiple files: zopflipng --prefix a.png b.png c.png\n"
         "Compress really good and trying all filter strategies: zopflipng"
         " --iterations=500 --filters=01234mywebipg --lossy_8bit"
         " --alpha_cleaner=012345 infile.png outfile.png\n");
}

void PrintSize(const char* label, size_t size) {
  printf("%s: %d (%dK)\n", label, (int) size, (int) size / 1024);
}

void PrintResultSize(const char* label, size_t oldsize, size_t newsize) {
  printf("%s: %d (%dK). Percentage of original: %.3f%%\n",
         label, (int) newsize, (int) newsize / 1024, newsize * 100.0 / oldsize);
}

int main(int argc, char *argv[]) {
printf("ZopfliPNG, a Portable Network Graphics (PNG) image optimizer.\n"
         "KrzYmod extends ZopfliPNG functionality - version 16\n\n");
  if (argc < 2) {
    ShowHelp();
    return 0;
  }

  ZopfliPNGOptions png_options;

  // cmd line options
  bool always_zopflify = false;  // overwrite file even if we have bigger result
  bool yes = false;  // do not ask to overwrite files
  bool dryrun = false;  // never save anything

  std::string user_out_filename;  // output filename if no prefix is used
  bool use_prefix = false;
  std::string prefix = "zopfli_";  // prefix for output filenames

  std::vector<std::string> files;

  signal(SIGINT, intHandlerpng);

  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg[0] == '-' && arg.size() > 1 && arg[1] != '-') {
      for (size_t pos = 1; pos < arg.size(); pos++) {
        char c = arg[pos];
        if (c == 'y') {
          yes = true;
        } else if (c == 'd') {
          dryrun = true;
        } else if (c == 'm') {
          png_options.num_iterations *= 4;
          png_options.num_iterations_large *= 4;
        } else if (c == 'q') {
          png_options.use_zopfli = false;
        } else if (c == 'h') {
          ShowHelp();
          return 0;
        } else {
          printf("Unknown flag: %c\n", c);
          return 0;
        }
      }
    } else if (arg[0] == '-' && arg.size() > 1 && arg[1] == '-') {
      size_t eq = arg.find('=');
      std::string name = arg.substr(0, eq);
      std::string value = eq >= arg.size() - 1 ? "" : arg.substr(eq + 1);
      int num = atoi(value.c_str());
      if (name == "--always_zopflify") {
        always_zopflify = true;
      } else if (name == "--alpha_cleaner") {
        for (size_t j = 0; j < value.size(); j++) {
          signed char f = value[j] - '0';
          if (f >= 0 && f <= 5) {
            png_options.lossy_transparent |= (1 << f);
          } else {
            printf("Unknown alpha cleaning method: %i\n", f);
            return 1;
          }
        }
      } else if (name == "--lossy_transparent") {
        png_options.lossy_transparent |= 2;
      } else if (name == "--lossy_8bit") {
        png_options.lossy_8bit = true;
      } else if (name == "--brotli") {
        png_options.usebrotli = 1;
      } else if (name == "--lazy") {
        png_options.lazymatching = 1;
      } else if (name == "--rc") {
        png_options.revcounts = 1;
      } else if (name == "--rp") {
        png_options.restorepoints = 1;
      } else if (name == "--ohh") {
        png_options.optimizehuffmanheader = 1;
      } else if (name == "--all") {
        png_options.tryall = 1;
      } else if (name == "--iterations") {
        if (num < 1) num = 1;
        png_options.num_iterations = num;
        png_options.num_iterations_large = num;
      } else if (name == "--mb") {
        if (num < 0) num = 15;
        png_options.blocksplittingmax = num;
      } else if (name == "--mls") {
        if (num < 1) num = 1024;
        png_options.lengthscoremax = num;
      } else if (name == "--pass") {
        if (num < 1) num = 1;
        png_options.pass = num;
      } else if (name == "--bsr") {
        if (num < 2) num = 9;
        png_options.findminimumrec = num;
      } else if (name == "--mui") {
        if (num < 0) num = 0;
        png_options.maxfailiterations = num;
      } else if (name == "--v") {
        if (num < 0) num = 1;
        png_options.verbosezopfli = num;
      } else if (name == "--rw") {
        if (num < 1) num = 1;
        png_options.ranstatew = num;
      } else if (name == "--rz") {
        if (num < 1) num = 1;
        png_options.ranstatez = num;
      } else if (name == "--idle") {
        IdlePriority();
      } else if (name == "--t") {
        png_options.numthreads = num;
      } else if (name == "--splitting") {
        // ignored
      } else if (name == "--nosplitlast") {
        png_options.noblocksplittinglast = 1;
      } else if (name == "--slowsplit") {
        png_options.slowsplit = 1;
      } else if (name == "--filters") {
        for (size_t j = 0; j < value.size(); j++) {
          ZopfliPNGFilterStrategy strategy = kStrategyZero;
          char f = value[j];
          switch (f) {
            case '0': strategy = kStrategyZero; break;
            case '1': strategy = kStrategyOne; break;
            case '2': strategy = kStrategyTwo; break;
            case '3': strategy = kStrategyThree; break;
            case '4': strategy = kStrategyFour; break;
            case 'm': strategy = kStrategyMinSum; break;
            case 'y': strategy = kStrategyDistinctBytes; break;
            case 'w': strategy = kStrategyDistinctBigrams; break;
            case 'e': strategy = kStrategyEntropy; break;
            case 'b': strategy = kStrategyBruteForce; break;
            case 'i': strategy = kStrategyIncremental; break;
            case 'p': strategy = kStrategyPredefined; break;
            case 'g': strategy = kStrategyGeneticAlgorithm; break;
            default:
              printf("Unknown filter strategy: %c\n", f);
              return 1;
          }
          png_options.filter_strategies.push_back(strategy);
          // Enable auto filter strategy only if no user-specified filter is
          // given.
          png_options.auto_filter_strategy = false;
        }
      } else if (name == "--zopfli_filters") {
          png_options.auto_filter_strategy = false;
      } else if (name == "--keepchunks") {
        bool correct = true;
        if ((value.size() + 1) % 5 != 0) correct = false;
        for (size_t i = 0; i + 4 <= value.size() && correct; i += 5) {
          png_options.keepchunks.push_back(value.substr(i, 4));
          if (i > 4 && value[i - 1] != ',') correct = false;
        }
        if (!correct) {
          printf("Error: keepchunks format must be like for example:\n"
                 " --keepchunks=gAMA,cHRM,sRGB,iCCP\n");
          return 0;
        }
      } else if (name == "--prefix") {
        use_prefix = true;
        if (!value.empty()) prefix = value;
      } else if (name == "--ga_population_size") {
        if (num < 1) num = 1;
        png_options.ga_population_size = num;
      } else if (name == "--ga_max_evaluations") {
        if (num < 0) num = 0;
        png_options.ga_max_evaluations = num;
      } else if (name == "--ga_stagnate_evaluations") {
        if (num < 1) num = 1;
        png_options.ga_stagnate_evaluations = num;
      } else if (name == "--ga_mutation_probability") {
        if (num < 0) num = 0;
          else if (num > 1) num = 1;
        png_options.ga_mutation_probability = num;
      } else if (name == "--ga_crossover_probability") {
        if (num < 0) num = 0;
          else if (num > 1) num = 1;
        png_options.ga_crossover_probability = num;
      } else if (name == "--ga_number_of_offspring") {
        if (num < 1) num = 1;
        png_options.ga_number_of_offspring = num;
      } else if (name == "--help") {
        ShowHelp();
        return 0;
      } else {
        printf("Unknown flag: %s\n", name.c_str());
        return 0;
      }
    } else {
      files.push_back(argv[i]);
    }
  }

  if (!use_prefix) {
    if (files.size() == 2) {
      // The second filename is the output instead of an input if no prefix is
      // given.
      user_out_filename = files[1];
      files.resize(1);
    } else {
      printf("Please provide one input and output filename\n\n");
      ShowHelp();
      return 0;
    }
  }

  size_t total_in_size = 0;
  // Total output size, taking input size if the input file was smaller
  size_t total_out_size = 0;
  // Total output size that zopfli produced, even if input was smaller, for
  // benchmark information
  size_t total_out_size_zopfli = 0;
  size_t total_errors = 0;
  size_t total_files = 0;
  size_t total_files_smaller = 0;
  size_t total_files_saved = 0;
  size_t total_files_equal = 0;

  for (size_t i = 0; i < files.size(); i++) {
    if (use_prefix && files.size() > 1) {
      std::string dir, file, ext;
      GetFileNameParts(files[i], &dir, &file, &ext);
      // avoid doing filenames which were already output by this so that you
      // don't get zopfli_zopfli_zopfli_... files after multiple runs.
      if (file.find(prefix) == 0) continue;
    }

    total_files++;

    printf("Optimizing %s\n", files[i].c_str());
    std::vector<unsigned char> image;
    unsigned w, h;
    std::vector<unsigned char> origpng;
    unsigned error;
    lodepng::State inputstate;
    std::vector<unsigned char> resultpng;

    error = lodepng::load_file(origpng, files[i]);
    if (!error) {
      error = ZopfliPNGOptimize(origpng, png_options,
                                (png_options.verbosezopfli!=0), &resultpng);
    }

    if (error) {
      if (error == 1) {
        printf("Decoding error\n");
      } else {
        printf("Decoding error %u: %s\n", error, lodepng_error_text(error));
      }
    }

    // Verify result, check that the result causes no decoding errors
    if (!error) {
      error = lodepng::decode(image, w, h, inputstate, resultpng);
      if (error) {
        printf("Error: verification of result failed. Error: %u.\n", error);
      }
    }

    if (error) {
      total_errors++;
    } else {
      size_t origsize = origpng.size();
      size_t resultsize = resultpng.size();

      if (!png_options.keepchunks.empty()) {
        std::vector<std::string> names;
        std::vector<size_t> sizes;
        lodepng::getChunkInfo(names, sizes, resultpng);
        for (size_t i = 0; i < names.size(); i++) {
          if (names[i] == "bKGD" || names[i] == "sBIT") {
            printf("Forced to keep original color type due to keeping bKGD or"
                   " sBIT chunk. Try without --keepchunks for better"
                   " compression.\n");
            break;
          }
        }
      }

      PrintSize("Input size", origsize);
      PrintResultSize("Result size", origsize, resultsize);
      if (resultsize < origsize) {
        printf("Result is smaller\n");
      } else if (resultsize == origsize) {
        printf("Result has exact same size\n");
      } else {
        printf(always_zopflify
            ? "Original was smaller\n"
            : "Preserving original PNG since it was smaller\n");
      }

      std::string out_filename = user_out_filename;
      if (use_prefix) {
        std::string dir, file, ext;
        GetFileNameParts(files[i], &dir, &file, &ext);
        out_filename = dir;
        out_filename += prefix;
        out_filename += file;
        out_filename += ext;
      }
      bool different_output_name = out_filename != files[i];

      total_in_size += origsize;
      total_out_size_zopfli += resultpng.size();
      if (resultpng.size() < origsize) total_files_smaller++;
      else if (resultpng.size() == origsize) total_files_equal++;

      if (!always_zopflify && resultpng.size() >= origsize) {
        // Set output file to input since zopfli didn't improve it.
        resultpng = origpng;
      }

      bool already_exists = FileExists(out_filename);
      size_t origoutfilesize = GetFileSize(out_filename);

      // When using a prefix, and the output file already exist, assume it's
      // from a previous run. If that file is smaller, it may represent a
      // previous run with different parameters that gave a smaller PNG image.
      // This also applies when not using prefix but same input as output file.
      // In that case, do not overwrite it. This behaviour can be removed by
      // adding the always_zopflify flag.
      bool keep_earlier_output_file = already_exists &&
          resultpng.size() >= origoutfilesize && !always_zopflify &&
          (use_prefix || !different_output_name);

      if (keep_earlier_output_file) {
        // An output file from a previous run is kept, add that files' size
        // to the output size statistics.
        total_out_size += origoutfilesize;
        if (use_prefix) {
          printf(resultpng.size() == origoutfilesize
              ? "File not written because a previous run was as good.\n"
              : "File not written because a previous run was better.\n");
        }
      } else {
        bool confirmed = true;
        if (!yes && !dryrun && already_exists) {
          printf("File %s exists, overwrite? (y/N) ", out_filename.c_str());
          char answer = 0;
          // Read the first character, the others and enter with getchar.
          while (int input = getchar()) {
            if (input == '\n' || input == EOF) break;
            else if (!answer) answer = input;
          }
          confirmed = answer == 'y' || answer == 'Y';
        }
        if (confirmed) {
          if (!dryrun) {
            if (lodepng::save_file(resultpng, out_filename) != 0) {
              printf("Failed to write to file %s\n", out_filename.c_str());
            } else {
              total_files_saved++;
            }
          }
          total_out_size += resultpng.size();
        } else {
          // An output file from a previous run is kept, add that files' size
          // to the output size statistics.
          total_out_size += origoutfilesize;
        }
      }
    }
    printf("\n");
  }

  if (total_files > 1) {
    printf("Summary for all files:\n");
    printf("Files tried: %d\n", (int) total_files);
    printf("Files smaller: %d\n", (int) total_files_smaller);
    if (total_files_equal) {
      printf("Files equal: %d\n", (int) total_files_equal);
    }
    printf("Files saved: %d\n", (int) total_files_saved);
    if (total_errors) printf("Errors: %d\n", (int) total_errors);
    PrintSize("Total input size", total_in_size);
    PrintResultSize("Total output size", total_in_size, total_out_size);
    PrintResultSize("Benchmark result size",
                    total_in_size, total_out_size_zopfli);
  }

  if (dryrun) printf("No files were written because dry run was specified\n");

  return total_errors;
}
