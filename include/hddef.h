#ifndef _HDDEF_H_
#define _HDDEF_H_

#include "hdef.h"
#include "hlog.h"
#include "hcfg.h"
#include "hds.h"
#include "nng/nng.h"
#include "nng/protocol/reqrep0/rep.h"

#define HDIC_NAME "hdic"
#define HDIC_RET_BASE 0x00010000
#define HBASE_RET_HDIC_ 0x00000001
#define HBASE_RET_HDIC_SET 0x00000002

//
#define HDIC_VALTYPE_STRING 0
#define HDIC_VALTYPE_STRUCT 1

#define HDIC_ALLOC malloc
#define HDIC_FREE free

typedef struct hdval_ {
  uint8_t type;
  uint8_t ph0;
  uint16_t ph1;
  uint32_t ph2;
  hbuf_t buf;
} hdval_t;


typedef struct hdcfg_ {
  int port;
} hdcfg_t;


struct hhash_hdval_t_;
typedef struct hhash_hdval_t_ hhash_hdval_t_t;

typedef struct hdcontext_ hdcontext_t;
typedef int (*hdfunc_t)(hdcontext_t* c);
struct hdcontext_ {
  // in
  int user;
  hbuf_t argv;
  char* cmd;
  char* key;
  hdfunc_t cmdfunc;
  // data
  nng_socket sock;
  hdcfg_t  cfg;
  hhash_hdval_t_t* dic;
  // out
  int ret;
  hbuf_t retout;
};

#endif