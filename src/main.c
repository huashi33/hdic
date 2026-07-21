#include "hdfifo.h"
#include "hddef.h"
#include <string.h>
#include <stdlib.h>




static void hdic_add(hdcontext_t *c){ }
static void hdic_del(hdcontext_t *c){ }
static void hdic_get(hdcontext_t *c){ }
static void hdic_set(hdcontext_t *c){ }


// 命令-函数指针映射表
typedef struct {
  const char   *cmd;
  hdic_func_t   func;
} hdic_cmd_entry_t;

static const hdic_cmd_entry_t hdic_cmd_table[] = {
  { "add",   hdic_add   },
  { "del",   hdic_del   },
  { "get",   hdic_get   },
  { "set",   hdic_set   }
};
#define HDIC_CMD_TABLE_SIZE (sizeof(hdic_cmd_table) / sizeof(hdic_cmd_table[0]))

// 查表：根据 c->cmd 返回对应函数指针，未找到返回 NULL
static hdic_func_t hdic_find_func(hdcontext_t *c){
  if (!c || !c->cmd) return NULL;
  for (size_t i = 0; i < HDIC_CMD_TABLE_SIZE; i++) {
    if (strcmp(c->cmd, hdic_cmd_table[i].cmd) == 0)
      return hdic_cmd_table[i].func;
  }
  return NULL;
}



//cmdline: cmd tag argv[0] argv[1] argv[2]...
static int hdic_parse(uint8_t *cmdline,size_t s,hdcontext_t *c){
  int r = 0;

  // 重置 argv 缓冲区
  c->argv.len = 0;
  c->cmd = c->tag = NULL;

  char *buf = (char *)cmdline;
  char *end = buf + s;

  // 确保字符串以 '\0' 结尾（调用方保证 cmdline[s] 可写或已为 '\0'）
  // 用指针扫描方式原地解析，不修改 cmdline 内容之外的内存

  // 跳过前导空格
  while (buf < end && *buf == ' ') buf++;

  // 解析 cmd
  if (buf >= end) return HBASE_RET(HBASE_RET_BASE_HDIC, HBASE_RET_PARAM(1));
  c->cmd = buf;
  while (buf < end && *buf != ' ') buf++;
  if (buf < end) { *buf = '\0'; buf++; }

  // 跳过空格
  while (buf < end && *buf == ' ') buf++;

  // 解析 tag
  if (buf >= end) return HBASE_RET(HBASE_RET_BASE_HDIC, HBASE_RET_PARAM(2));
  c->tag = buf;
  while (buf < end && *buf != ' ') buf++;
  if (buf < end) { *buf = '\0'; buf++; }

  // 解析 argv（剩余所有 token，每个以 '\0' 结尾后 push 进 c->argv）
  // 支持双引号包裹含空格/特殊字符的 token，例如: "nanjing city"
  while (buf < end) {
    // 跳过空格
    while (buf < end && *buf == ' ') buf++;
    if (buf >= end) break;

    char *tok;
    size_t tok_len;

    if (*buf == '"') {
      // 带引号 token：跳过开头 '"'，找到匹配的结尾 '"'
      buf++;
      tok = buf;
      while (buf < end && *buf != '"') buf++;
      tok_len = (size_t)(buf - tok);
      // 跳过结尾 '"'，并在原地写入 '\0'
      if (buf < end) { *buf = '\0'; buf++; }
    } else {
      // 普通 token：以空格结尾
      tok = buf;
      while (buf < end && *buf != ' ') buf++;
      if (buf < end) { *buf = '\0'; buf++; }
      tok_len = (size_t)(buf - tok);
    }

    // push token（含结尾 '\0'）
    r = hbuf_push(&c->argv, tok, tok_len + 1);
    HBASE_RET_WHEN(r, HBASE_RET(HBASE_RET_BASE_HDIC, r));


  }



  c->cmdfunc = hdic_find_func(c);
  HBASE_RET_WHEN(!c->cmdfunc, HBASE_RET(HBASE_RET_BASE_HDIC, HBASE_RET_NOTFOUND));

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
  HBASE_RET_WHEN(!d, HBASE_RET(HBASE_RET_BASE_HDIC, HBASE_RET_PARAM(0)));
  HBASE_RET_WHEN(!s, HBASE_RET(HBASE_RET_BASE_HDIC, HBASE_RET_PARAM(1)));

  // parse cmd
  r = hdic_parse(d,s,c);
  HBASE_RET_WHEN(r,HBASE_RET(r,r));


  c->cmdfunc(c);

  // exec
  return HBASE_RET_OK;
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