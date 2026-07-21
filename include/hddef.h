#ifndef _HDDEF_H_
#define _HDDEF_H_

#include "hdef.h"
#include "hds.h"
#include "hlog.h"


#define HDIC_NAME "hdic"
#define HBASE_RET_BASE_HDIC 0x00010000






typedef struct hdcontext_{
  // in
  hbuf_t  argv;
  char*   cmd;
  char*   tag;
  // out
  int     ret;
  hbuf_t  retout;
}hdcontext_t;

#endif