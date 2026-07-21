#ifndef _HDDEF_H_
#define _HDDEF_H_

#include "hdef.h"
#include "hds.h"
#include "hlog.h"


#define HDIC_NAME "hdic"
#define HBASE_RET_BASE_HDIC 0x00010000
#define HBASE_RET_HDIC_ 0x00000001






typedef struct hdcontext_ hdcontext_t;
typedef void (*hdic_func_t)(hdcontext_t *c);

struct hdcontext_{
  // in
  hbuf_t        argv;
  char*         cmd;
  char*         tag;
  hdic_func_t   cmdfunc;
  // out
  int           ret;
  hbuf_t        retout;
};

#endif