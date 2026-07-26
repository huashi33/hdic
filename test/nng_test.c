/*
 * nng_test.c —— hdic nng(REQ/REP) 接口读写性能测试
 *
 * 用法:
 *   ./nng_test [url] [iterations]
 *     url        : hdic 监听地址，默认 ipc:///tmp/hdic.sock
 *     iterations : 每项测试的往返次数，默认 100000
 *
 * 测试内容:
 *   1) 写入: 循环发送 "set name zs"，期望应答 "ok"
 *   2) 读取: 循环发送 "get name"  ，期望应答 "name zs"
 *
 * 说明:
 *   REQ/REP 是严格一问一答，每次迭代 = 一次完整往返(send + recv)，
 *   因此统计出的是同步 RPC 的吞吐与平均往返延迟，而非单向写入速度。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "nng/nng.h"
#include "nng/protocol/reqrep0/req.h"

#define DEFAULT_URL "ipc:///tmp/hdic.sock"
#define DEFAULT_ITER 10000
#define RECV_TIMEO_MS 5000
#define BUF_SIZE 256

#define SET_CMD "set name zs"
#define SET_EXPECT "ok"
#define GET_CMD "get name"
#define GET_EXPECT "name zs"

static double now_sec(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* 一次往返统计 */
typedef struct {
  long ok;       /* 应答内容符合预期的次数 */
  long bad;      /* 应答内容不符合预期的次数 */
  long err;      /* 收发出错的次数 */
  double elapsed;/* 总耗时(秒) */
  double min_us; /* 最小往返延迟 */
  double max_us; /* 最大往返延迟 */
} stat_t;

/*
 * 循环执行 iter 次 "发送 cmd -> 接收应答" 往返。
 * expect 非 NULL 时，按前缀比对应答内容。
 */
static int bench(nng_socket sock, const char* cmd, const char* expect, long iter,
                 stat_t* st) {
  char buf[BUF_SIZE];
  size_t cmd_len = strlen(cmd);
  size_t expect_len = expect ? strlen(expect) : 0;

  memset(st, 0, sizeof(*st));
  st->min_us = 1e18;

  double t0 = now_sec();
  for (long i = 0; i < iter; i++) {
    double s0 = now_sec();

    int r = nng_send(sock, (void*)cmd, cmd_len, 0);
    if (r != 0) {
      if (st->err++ == 0) fprintf(stderr, "nng_send: %s\n", nng_strerror(r));
      continue;
    }

    /* 不带 NNG_FLAG_ALLOC: sz 传入缓冲区容量，返回实际长度，避免每次 malloc */
    size_t sz = sizeof(buf);
    r = nng_recv(sock, buf, &sz, 0);
    if (r != 0) {
      if (st->err++ == 0) fprintf(stderr, "nng_recv: %s\n", nng_strerror(r));
      continue;
    }

    double d_us = (now_sec() - s0) * 1e6;
    if (d_us < st->min_us) st->min_us = d_us;
    if (d_us > st->max_us) st->max_us = d_us;

    /* 应答可能带结尾 NUL(hdic_get 会 push 一个 '\0')，按前缀比对 */
    if (expect && (sz < expect_len || memcmp(buf, expect, expect_len) != 0)) {
      if (st->bad++ == 0) {
        buf[sz < sizeof(buf) ? sz : sizeof(buf) - 1] = '\0';
        fprintf(stderr, "应答不符: 期望前缀 \"%s\", 实收[%zu]\"%s\"\n", expect,
                sz, buf);
      }
    } else {
      st->ok++;
    }
  }
  st->elapsed = now_sec() - t0;
  if (st->min_us > 1e17) st->min_us = 0;
  return st->err ? -1 : 0;
}

static void report(const char* name, const char* cmd, long iter,
                   const stat_t* st) {
  double qps = (st->elapsed > 0) ? (double)iter / st->elapsed : 0;
  double avg_us = (iter > 0) ? st->elapsed * 1e6 / (double)iter : 0;
  printf("[%s] \"%s\" x %ld\n", name, cmd, iter);
  printf("  耗时    : %.3f s\n", st->elapsed);
  printf("  吞吐    : %.0f 次/秒\n", qps);
  printf("  平均延迟: %.2f us  (min %.2f / max %.2f)\n", avg_us, st->min_us,
         st->max_us);
  printf("  应答    : ok=%ld bad=%ld err=%ld\n\n", st->ok, st->bad, st->err);
}

int main(int argc, char* argv[]) {
  const char* url = (argc >= 2) ? argv[1] : DEFAULT_URL;
  long iter = (argc >= 3) ? strtol(argv[2], NULL, 10) : DEFAULT_ITER;
  if (iter <= 0) iter = DEFAULT_ITER;

  nng_socket sock;
  int r = nng_req0_open(&sock);
  if (r != 0) {
    fprintf(stderr, "nng_req0_open: %s\n", nng_strerror(r));
    return EXIT_FAILURE;
  }

  /* 防止服务端不回复时无限阻塞 */
  r = nng_socket_set_ms(sock, NNG_OPT_RECVTIMEO, RECV_TIMEO_MS);
  if (r != 0) {
    fprintf(stderr, "set RECVTIMEO: %s\n", nng_strerror(r));
    nng_close(sock);
    return EXIT_FAILURE;
  }

  r = nng_dial(sock, url, NULL, 0);
  if (r != 0) {
    fprintf(stderr, "nng_dial(%s): %s\n", url, nng_strerror(r));
    fprintf(stderr, "请先启动 hdic 服务端，并确认 url 与服务端 listen 的完全一致\n");
    nng_close(sock);
    return EXIT_FAILURE;
  }

  printf("hdic nng 接口性能测试\n");
  printf("  地址    : %s\n", url);
  printf("  迭代次数: %ld\n\n", iter);

  /* 预热: 建连、首次 set 插入 key，避免把建连开销算进统计 */
  stat_t warm;
  if (bench(sock, SET_CMD, SET_EXPECT, 1, &warm) != 0) {
    fprintf(stderr, "预热失败，服务端可能未正常处理请求\n");
    nng_close(sock);
    return EXIT_FAILURE;
  }

  stat_t st_set, st_get;
  int rc = EXIT_SUCCESS;

  if (bench(sock, SET_CMD, SET_EXPECT, iter, &st_set) != 0) rc = EXIT_FAILURE;
  report("写入", SET_CMD, iter, &st_set);

  if (bench(sock, GET_CMD, GET_EXPECT, iter, &st_get) != 0) rc = EXIT_FAILURE;
  report("读取", GET_CMD, iter, &st_get);

  double total = st_set.elapsed + st_get.elapsed;
  printf("[合计] %ld 次往返, 耗时 %.3f s, 平均 %.0f 次/秒\n", iter * 2, total,
         total > 0 ? (double)(iter * 2) / total : 0);

  if (st_set.bad || st_get.bad || st_set.err || st_get.err) rc = EXIT_FAILURE;

  nng_close(sock);
  return rc;
}
