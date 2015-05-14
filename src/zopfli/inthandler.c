#include <stdio.h>
#include "inthandler.h"

void intHandler(int exit_code) {
  if(exit_code==2) {
    fprintf(stderr,"                                                              \n"
                   " (!!) CTRL+C detected! Setting --mui to 1 to finish work ASAP!\n"
                   " (!!) Press it again to abort work.\n");
    mui=1;
  }
}
