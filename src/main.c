#include <time.h>

#include "hddef.h"

// key:user's key;val:hdval_t
HHASH_DEFINE(char*, hdval_t)
// key:cmd,val;hdic_func_t
HHASH_DEFINE(char*, hdfunc_t)

// func
static int hdic_val_string_new(hdval_t* val, int type, char* vbuf, size_t s) {
  int r = 0;
  val->type = type;
  r = hbuf_init(&val->buf, s);
  HC_RET_WHEN(r, r);
  if (vbuf) {
    r = hbuf_push(&val->buf, vbuf, s);
    // HC_RET_WHEN(r, r);
  }
  return r;
}
static int hdic_val_string_free(hdval_t* val) {
  int r = 0;
  if (val && val->buf.size) {
    r = hbuf_deinit(&val->buf);
    HC_RET_WHEN(r, r);
  }
  return r;
}
static int hdic_del(hdcontext_t* c) {
  size_t argc = c->argv.len / sizeof(char*);
  char** argv = (char**)c->argv.data;
  HC_RET_WHEN(!argc, HDIC_RET(0, HC_RET_HDIC_PARAMNUM,0));

  int r = 0;
  hbuf_clear(&c->retout);
  for (size_t i = 0; i < argc; i++){
    r = hhash_del_hdval_t(c->dic, argv[i]);
    if(HC_RET_OK == r){
      hbuf_push(&c->retout, argv[i],strlen(argv[i]));
      hbuf_push(&c->retout," ",1);
    }
  }
  return HC_RET_OK;
}
static int hdic_get(hdcontext_t* c) {
  size_t argc = c->argv.len / sizeof(char*);
  char** argv = (char**)c->argv.data;
  HC_RET_WHEN(!argc , HDIC_RET(0, HC_RET_HDIC_PARAMNUM,0));


  int r = 0;
  hbuf_clear(&c->retout);
  for (size_t i = 0; i < argc; i++){
    hdval_t* val = hhash_get_hdval_t(c->dic, argv[i]);
    if(val){
      if(c->retout.len){
        hbuf_push(&c->retout, " ", 1);
      }
      hbuf_push(&c->retout, argv[i], strlen(argv[i]));
      hbuf_push(&c->retout, " ", 1);
      hbuf_push(&c->retout, val->buf.data, val->buf.len);
    }
  }
  return c->retout.len ? HC_RET_OK : HDIC_RET(0, HC_RET_NOTFOUND,0);
}
static int hdic_set(hdcontext_t* c) {
  size_t argc = c->argv.len / sizeof(char*);
  char** argv = (char**)c->argv.data;
  HC_RET_WHEN(!argc || 0!=argc%2, HDIC_RET(0, HC_RET_HDIC_PARAMNUM,0));

  int r = 0;
  hbuf_clear(&c->retout);
  size_t pair = argc /2;
  for (size_t i = 0; i < pair; i++){
    char* k = argv[2*i];
    char* v = argv[2*i+1];
    hdval_t* val = hhash_get_hdval_t(c->dic, k);
    if(val){
      hbuf_clear(&val->buf);
      r = hbuf_push(&val->buf, v, strlen(v));
      if(HC_RET_OK == r){
        hbuf_push(&c->retout, k, strlen(k));
        hbuf_push(&c->retout, " ", 1);
      }
    }
  }
  return HC_RET_OK;
}
static int hdic_add(hdcontext_t* c) {
  size_t argc = c->argv.len / sizeof(char*);
  char** argv = (char**)c->argv.data;
  HC_RET_WHEN(!argc, HDIC_RET(0,HC_RET_HDIC_PARAMNUM,0));

  int r = 0;
  for (size_t i = 0; i < argc; i++){
    hdval_t* val = hhash_get_hdval_t(c->dic, argv[i]);
    if (!val){
      hdval_t val;
      r = hdic_val_string_new(&val, HDIC_VALTYPE_STRING,NULL,0);
      r = hhash_set_hdval_t(c->dic, argv[i], val);
      HC_RET_WHEN(!r, HDIC_RET(i,HC_RET_HDIC_SUBMOD,r));
    }
  }
  return HC_RET_OK;
}
static int hdic_findfunc(char* cmd, hdcontext_t* c) {
  c->cmdfunc = NULL;
  static hhash_hdfunc_t* hashfunc = NULL;
  if (!hashfunc) {
    hashfunc = hhash_init_hdfunc_t();
    hhash_set_hdfunc_t(hashfunc, "add", hdic_add);
    hhash_set_hdfunc_t(hashfunc, "set", hdic_set);
    hhash_set_hdfunc_t(hashfunc, "get", hdic_get);
    hhash_set_hdfunc_t(hashfunc, "del", hdic_del);
  }

  hdfunc_t f = *(hhash_get_hdfunc_t(hashfunc, cmd));
  c->cmdfunc = f;
  return f ? HC_RET_OK : HC_RET(HDIC_RET_BASE, HC_RET_NOTFOUND);

}
// cmdline: cmd tag argv[0] argv[1] argv[2]...
static int hdic_parse(uint8_t* cmdline, size_t s, hdcontext_t* c) {
  int r = 0;

  // 重置 argv 缓冲区
  hbuf_clear(&c->argv);
  c->cmd = NULL;

  char* buf = (char*)cmdline;
  char* end = buf + s;

  // 确保字符串以 '\0' 结尾（调用方保证 cmdline[s] 可写或已为 '\0'）
  // 用指针扫描方式原地解析，不修改 cmdline 内容之外的内存

  // 跳过前导空格
  while (buf < end && *buf == ' ') buf++;

  // 解析 cmd
  if (buf >= end) return HC_RET(HDIC_RET_BASE, HC_RET_PARAM(1));
  c->cmd = buf;
  while (buf < end && *buf != ' ') buf++;
  if (buf < end) {
    *buf = '\0';
    buf++;
  }

  // 跳过空格
  while (buf < end && *buf == ' ') buf++;

  // 解析 KEY
  // if (buf >= end) return HC_RET(HDIC_RET_BASE, HC_RET_PARAM(2));
  // c->key = buf;
  // while (buf < end && *buf != ' ') buf++;
  // if (buf < end) {
  //   *buf = '\0';
  //   buf++;
  // }

  // 解析 argv（剩余所有 token，每个以 '\0' 结尾后 push 进 c->argv）
  // 支持双引号包裹含空格/特殊字符的 token，例如: "nanjing city"
  while (buf < end) {
    // 跳过空格
    while (buf < end && *buf == ' ') buf++;
    if (buf >= end) break;

    char* tok;
    size_t tok_len;

    if (*buf == '"') {
      // 带引号 token：跳过开头 '"'，找到匹配的结尾 '"'
      buf++;
      tok = buf;
      while (buf < end && *buf != '"') buf++;
      tok_len = (size_t)(buf - tok);
      // 跳过结尾 '"'，并在原地写入 '\0'
      if (buf < end) {
        *buf = '\0';
        buf++;
      }
    } else {
      // 普通 token：以空格结尾
      tok = buf;
      while (buf < end && *buf != ' ') buf++;
      if (buf < end) {
        *buf = '\0';
        buf++;
      }
      tok_len = (size_t)(buf - tok);
    }

    // push token 指针（char*）到 c->argv
    r = hbuf_push(&c->argv, &tok, sizeof(char*));
    HC_RET_WHEN(r, HC_RET(HDIC_RET_BASE, r));
  }



  return hdic_findfunc(c->cmd, c);
}
static int hdcontext_init(hdcontext_t* c) {
  HC_RET_WHEN(!c, HC_RET(HDIC_RET_BASE, HC_RET_PARAM(0)));
  int r = 0;

  r = hbuf_init(&c->argv, 0);
  HC_RET_WHEN(r, HC_RET(HDIC_RET_BASE, r));
  r = hbuf_init(&c->retout, 0);
  HC_RET_WHEN(r, HC_RET(HDIC_RET_BASE, r));
  c->dic = hhash_init_hdval_t();
  HC_RET_WHEN(!c->dic, HC_RET(HDIC_RET_BASE, HC_RET_MALLOC));

  c->ret = 1;
  c->cmd = NULL;
  return r;
}
static int hdcontext_deinit(hdcontext_t* c) {
  HC_RET_WHEN(!c, HC_RET(HDIC_RET_BASE, HC_RET_PARAM(0)));
  int r = 0;
  r = hbuf_deinit(&c->argv);
  HC_RET_WHEN(r, HC_RET(HDIC_RET_BASE, r));
  r = hbuf_deinit(&c->retout);
  HC_RET_WHEN(r, HC_RET(HDIC_RET_BASE, r));
  r = hhash_deinit_hdval_t(c->dic);
  HC_RET_WHEN(r, HC_RET(HDIC_RET_BASE, r));
  c->dic = NULL;

  c->ret = 1;
  c->cmd  = NULL;
  return r;
}

static double now_sec(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}
// single thread,process a frame
static int hdic_process(uint8_t* d, size_t s, hdcontext_t* c) {
  int r = 0;
  HC_RET_WHEN(!d, HC_RET(HDIC_RET_BASE, HC_RET_PARAM(0)));
  HC_RET_WHEN(!s, HC_RET(HDIC_RET_BASE, HC_RET_PARAM(1)));

  // parse cmd
  r = hdic_parse(d, s, c);
  HC_RET_WHEN(r, HC_RET(r, r));
  return c->cmdfunc(c);
}

// read cfg
// init log
static int hdic_init(const char* conf, hdcontext_t* ctx) {
  int r = 0;
  r = hdcontext_init(ctx);
  HC_EXEC_RET_WHEN(r, fprintf(stderr, "[%08X]hdcontext_init\n", r),
                      HC_RET(HDIC_RET_BASE, r));

  hcfg_t cfghandle;
  r = hcfg_init(&cfghandle, conf, HCFG_TYPE_INI);
  HC_EXEC_RET_WHEN(r, fprintf(stderr, "[%08X]hcfg_init\n", r),
                      HC_RET(HDIC_RET_BASE, r));

  char tmp[256] = {0};
  r = hcfg_str(&cfghandle, "basic:log", tmp, sizeof(tmp));
  HC_EXEC_RET_WHEN(r, fprintf(stderr, "[%08X]hcfg_str\n", r);
                      hcfg_deinit(&cfghandle), HC_RET(HDIC_RET_BASE, r))
  r = hlog_init(HDIC_NAME, tmp);
  HLOG_INFO("[%08X]hlog_init:%s", r, tmp);

  r = hcfg_str(&cfghandle, "net:port", tmp, sizeof(tmp));
  HC_EXEC_RET_WHEN(r, HLOG_ERROR("[%08X]hlog_init:%s", r, tmp);
                      hcfg_deinit(&cfghandle), HC_RET(HDIC_RET_BASE, r))
  ctx->cfg.port = atoi(tmp);

  hcfg_deinit(&cfghandle);
  // log cfg
  HLOG_INFO("[%08X]net.port:%d", r, ctx->cfg.port);



  return HC_RET_OK;
}
static int hdic_deinit(hdcontext_t* ctx) {
  int r = 0;
  r = hdcontext_deinit(ctx);
  HC_RET_WHEN(r, HC_RET(HDIC_RET_BASE, r));

  return 0;
}
static int hdic_exec(hdcontext_t* ctx) {
  int r = 0;

  // init nng
  char url[256] = {0};
  // snprintf(url, sizeof(url) - 1, "ipc://0.0.0.0:%d", ctx->cfg.port);
  snprintf(url, sizeof(url) - 1, "ipc:///tmp/hdic.sock");
  HLOG_INFO("opne %s", url);

  r = nng_rep0_open(&ctx->sock);
  HC_EXEC_RET_WHEN(r, HLOG_ERROR("[%d]nng_rep0_open", r), r);
  r = nng_listen(ctx->sock, url, NULL, 0);
  HC_EXEC_RET_WHEN(r, HLOG_ERROR("[%d]nng_listen", r);
                      nng_close(ctx->sock), r);

  char request[256];
  char response[256];
  size_t sz = sizeof(request);
  while (1) {
    sz = sizeof(request);
    // recv
    if (r = nng_recv(ctx->sock, &request, &sz, 0)) {
      HLOG_ERROR("[%d]nng_recv", r);
      break;
    }

    request[sz] = 0;
    HLOG_DEBUG("< [%zu]%s", sz, request);
    // process
    // snprintf(response, sizeof(response), "Response to: %s", request);
    ctx->ret = hdic_process(request,sz,ctx);
    HLOG_DEBUG("> [%08X]%s",ctx->ret,ctx->retout.data);


    // send
    if (r = nng_send(ctx->sock, ctx->retout.data, ctx->retout.len, 0)) {
      HLOG_ERROR("[%d]nng_send", r);
      break;
    }
  }

  nng_close(ctx->sock);
  return HC_RET_OK;
}



int main(size_t argc, char* argv[]) {
  // check param
  const char* conf = argv[1];
  HC_RET_WHEN(!conf, HC_RET(HDIC_RET_BASE, HC_RET_PARAM(0)));

  // init
  int r = 0;
  hdcontext_t ctx;
  r = hdic_init(conf, &ctx);
  HC_RET_WHEN(r, HC_RET(HDIC_RET_BASE, r));
  HLOG_INFO("[%08X]hdic_init", r);

  // exec
  r = hdic_exec(&ctx);
  HLOG_INFO("[%08X]hdic_exec", r);
  HC_RET_WHEN(r, HC_RET(HDIC_RET_BASE, r));

  r = hdic_deinit(&ctx);
  return r;
}