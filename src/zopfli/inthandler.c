#include "defines.h"
#include <stdio.h>
#include "inthandler.h"

unsigned int mui;

void intHandler(int exit_code) {
  if(exit_code==2) {
    fprintf(stderr,"                                                              \n"
                   " (!!) CTRL+C detected! Setting --mui to 1 to finish work ASAP!\n"
                   " (!!) Restore points won't be saved from now on!              \n"
                   " (!!) Press it again to abort work.\n");
    mui=1;
  }
}
