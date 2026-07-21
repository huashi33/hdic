#include "hdfifo.h"
#include "hddef.h"
#include <string.h>
#include <stdlib.h>





static int hdic_parse(uint8_t *d,size_t s,hdcontext_t *c){
  int r =0;


  return r;
}
static int hdic_exec(hdcontext_t *c){
  int r = 0;

  c->ret = 0;
  hbuf_push(&c->retout,"tag1 25",7);
  return r;
}
static int hdcontext_init(hdcontext_t *c){
  HBASE_RET_WHEN(!c,HBASE_RET(HBASE_RET_BASE_HDIC,HBASE_RET_PARAM(0)));
  int r = 0;

  r = hbuf_init(&c->argv,0);
  HBASE_RET_WHEN(r,HBASE_RET(HBASE_RET_BASE_HDIC,r));
  r = hbuf_init(&c->retout,0);
  HBASE_RET_WHEN(r,HBASE_RET(HBASE_RET_BASE_HDIC,r));
  
  c->ret = 1;
  c->cmd = c->tag = NULL;
  return r;
}
static int hdcontext_deinit(hdcontext_t *c){
  HBASE_RET_WHEN(!c,HBASE_RET(HBASE_RET_BASE_HDIC,HBASE_RET_PARAM(0)));
  int r = 0;
  r = hbuf_deinit(&c->argv);
  HBASE_RET_WHEN(r,HBASE_RET(HBASE_RET_BASE_HDIC,r));
  r = hbuf_deinit(&c->retout);
  HBASE_RET_WHEN(r,HBASE_RET(HBASE_RET_BASE_HDIC,r));
  c->ret = 1;
  c->cmd = c->tag = NULL;
  return r;
}
// single thread,process a frame
static int hdic_process(uint8_t *d,size_t s,hdcontext_t *c){
  int r = 0;
  // parse cmd
  r = hdic_parse(d,s,c);
  HBASE_RET_WHEN(r,HBASE_RET(r,r));
  // exec
  r = hdic_exec(c);
  return r;
}







static int hdraw_main(int argc,char *argv[],hdcontext_t *c,int (*process)(uint8_t* d,size_t,hdcontext_t *c)){
  int r = 0;
  const char* cmd = argv[2];
  HLOG_INFO("< %s",cmd);
  r = process((uint8_t*)cmd,strlen(cmd),c);
  HLOG_INFO("> [%d]%s",c->ret,c->retout.data);
  // there is no write-back
  return r;
}
int main(int argc,char *argv[]){
  HBASE_RET_WHEN(1==argc,HBASE_RET(HBASE_RET_BASE_HDIC,HBASE_RET_PARAM(0)));
  
  int r = 0;
  r = hlog_init(HDIC_NAME,"../config/hdic_log.conf");
  hdcontext_t c;
  r = hdcontext_init(&c);
  HLOG_INFO("[%08X]hdcontext_init",r);
  HBASE_RET_WHEN(r,HBASE_RET(HBASE_RET_BASE_HDIC,r));


  
  if(0 == strcmp("raw",argv[1])){
    return hdraw_main(argc,argv,&c,hdic_process);
  }
  else if(0 == strcmp("fifo",argv[1])){
    return hdfifo_main(argc,argv,&c,hdic_process);
  }
  else{

  }

  r = hdcontext_deinit(&c);
  return r;
}