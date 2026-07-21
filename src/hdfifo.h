#ifndef _HDFIFO_
#define _HDFIFO_
#include "hddef.h"
int hdfifo_main(int argc, char *argv[],hdcontext_t *c,int (*process)(uint8_t*,size_t,hdcontext_t *c));

#endif
