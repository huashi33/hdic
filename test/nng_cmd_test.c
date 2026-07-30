/*
 * nng_cmd_test.c —— hdic 各条指令(add/set/get/del)的往返耗时测试
 *
 * 用法:
 *   ./nng_cmd_test [url] [iterations]
 *     url        : hdic 监听地址，默认 ipc:///tmp/hdic.sock
 *     iterations : 每条指令的往返次数，默认 10000
 *
 * 测试内容(对应 README 中描述的指令):
 *   单键组，每次迭代使用互不相同的 key，避免命中同一条哈希项:
 *     add k<i>
 *     set k<i> v<i>
 *     get k<i>              -> 期望 "k<i> v<i>"
 *     del k<i>
 *   多键组，一条指令携带 32 个 key:
 *     add  a0_<i> a1_<i> ... a31_<i>
 *     set  a0_<i> v0_<i> a1_<i> v1_<i> ... a31_<i> v31_<i>
 *     get  a0_<i> ... a31_<i>   -> 期望 "a0_<i> v0_<i> ... a31_<i> v31_<i>"
 *     del  a0_<i> ... a31_<i>
 *
 * 说明:
 *   1) REQ/REP 一问一答，每次迭代 = send + recv 一个完整往返，
 *      统计的是同步 RPC 的往返延迟，包含 IPC 开销，不是纯指令执行时间。
 *   2) 服务端 hdic_add() 不写 retout，因此 add 的应答内容是上一条指令的
 *      残留数据，这里不校验 add 的应答内容，只统计耗时。
 *   3) 服务端只回发 retout，不回发错误码，故失败只能通过应答内容判断。
 *   4) 32 键的 set 命令长度可达 700+ 字节，超过服务端 hdic_exec() 里
 *      char request[256] 的接收缓冲；服务端缓冲不放大时该组会 bad/err。
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
/* 多键组每条指令携带的 key 个数 */
#define MKEYS 32
/* 32 键的 set 命令与 get 应答都可达 700+ 字节，缓冲要够大 */
#define BUF_SIZE 4096

static double now_sec(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

typedef struct {
  const char* name; /* 指令名 */
  const char* demo; /* 示例命令行，仅用于打印 */
  long ok;
  long bad;
  long err;
  double elapsed; /* 该指令全部迭代的总耗时(秒) */
  double* lat_us; /* 每次往返的延迟(微秒) */
  long lat_n;
} stat_t;

static int cmp_double(const void* a, const void* b) {
  double x = *(const double*)a, y = *(const double*)b;
  return (x > y) - (x < y);
}

static double pct(stat_t* st, double p) {
  if (st->lat_n <= 0) return 0;
  long idx = (long)(p * (double)(st->lat_n - 1) + 0.5);
  if (idx < 0) idx = 0;
  if (idx >= st->lat_n) idx = st->lat_n - 1;
  return st->lat_us[idx];
}

/* 多键组命令拼装: 第 i 次迭代的第 j 个 key 为 a<j>_<i>，值为 v<j>_<i>，
   保证每次迭代的 key 互不相同，不重复命中同一条哈希项 */

/* "<op> a0_<i> a1_<i> ... a31_<i>"，用于 add/get/del */
static void build_keys(char* out, size_t osz, const char* op, long i) {
  int n = snprintf(out, osz, "%s", op);
  for (int j = 0; j < MKEYS && n > 0 && (size_t)n < osz; j++)
    n += snprintf(out + n, osz - (size_t)n, " a%d_%ld", j, i);
}

/* "set a0_<i> v0_<i> ... a31_<i> v31_<i>" */
static void build_pairs(char* out, size_t osz, long i) {
  int n = snprintf(out, osz, "set");
  for (int j = 0; j < MKEYS && n > 0 && (size_t)n < osz; j++)
    n += snprintf(out + n, osz - (size_t)n, " a%d_%ld v%d_%ld", j, i, j, i);
}

/* set/del 的应答: 每个成功处理的 key 后跟一个空格 "a0_<i> a1_<i> ... " */
static void build_expect_keys(char* out, size_t osz, long i) {
  int n = 0;
  out[0] = '\0';
  for (int j = 0; j < MKEYS && (size_t)n < osz; j++)
    n += snprintf(out + n, osz - (size_t)n, "a%d_%ld ", j, i);
}

/* get 的应答: "a0_<i> v0_<i> a1_<i> v1_<i> ..."，键值之间单空格分隔 */
static void build_expect_pairs(char* out, size_t osz, long i) {
  int n = 0;
  out[0] = '\0';
  for (int j = 0; j < MKEYS && (size_t)n < osz; j++)
    n += snprintf(out + n, osz - (size_t)n, "%sa%d_%ld v%d_%ld", j ? " " : "",
                  j, i, j, i);
}

/* 一次往返: 发送 cmd，收应答；expect 非 NULL 时按前缀比对 */
static void once(nng_socket sock, stat_t* st, const char* cmd,
                 const char* expect) {
  char buf[BUF_SIZE];
  double t0 = now_sec();

  int r = nng_send(sock, (void*)cmd, strlen(cmd), 0);
  if (r != 0) {
    if (st->err++ == 0) fprintf(stderr, "nng_send(%s): %s\n", cmd, nng_strerror(r));
    return;
  }
  size_t sz = sizeof(buf);
  r = nng_recv(sock, buf, &sz, 0);
  if (r != 0) {
    if (st->err++ == 0) fprintf(stderr, "nng_recv(%s): %s\n", cmd, nng_strerror(r));
    return;
  }

  double d_us = (now_sec() - t0) * 1e6;
  st->elapsed += d_us / 1e6;
  st->lat_us[st->lat_n++] = d_us;

  if (expect) {
    size_t n = strlen(expect);
    if (sz < n || memcmp(buf, expect, n) != 0) {
      if (st->bad++ == 0) {
        buf[sz < sizeof(buf) ? sz : sizeof(buf) - 1] = '\0';
        fprintf(stderr, "应答不符: 命令\"%s\" 期望前缀\"%s\" 实收[%zu]\"%s\"\n",
                cmd, expect, sz, buf);
      }
      return;
    }
  }
  st->ok++;
}

static int stat_init(stat_t* st, const char* name, const char* demo, long iter) {
  memset(st, 0, sizeof(*st));
  st->name = name;
  st->demo = demo;
  st->lat_us = (double*)malloc(sizeof(double) * (size_t)iter);
  return st->lat_us ? 0 : -1;
}

static void stat_free(stat_t* st) {
  free(st->lat_us);
  st->lat_us = NULL;
}

static void report_head(const char* group) {
  printf("== %s ==\n", group);
  printf("%-5s %10s %12s %10s %10s %10s %10s %10s %s\n", "指令", "耗时(s)",
         "吞吐(次/s)", "平均(us)", "p50(us)", "p99(us)", "min(us)", "max(us)",
         "ok/bad/err");
}

static void report(stat_t* st) {
  if (st->lat_n > 0) qsort(st->lat_us, (size_t)st->lat_n, sizeof(double), cmp_double);
  double avg = st->lat_n ? st->elapsed * 1e6 / (double)st->lat_n : 0;
  double qps = st->elapsed > 0 ? (double)st->lat_n / st->elapsed : 0;
  printf("%-5s %10.3f %12.0f %10.2f %10.2f %10.2f %10.2f %10.2f %ld/%ld/%ld\n",
         st->name, st->elapsed, qps, avg, pct(st, 0.50), pct(st, 0.99),
         pct(st, 0.0), pct(st, 1.0), st->ok, st->bad, st->err);
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
  r = nng_socket_set_ms(sock, NNG_OPT_RECVTIMEO, RECV_TIMEO_MS);
  if (r != 0) {
    fprintf(stderr, "set RECVTIMEO: %s\n", nng_strerror(r));
    nng_close(sock);
    return EXIT_FAILURE;
  }
  r = nng_dial(sock, url, NULL, 0);
  if (r != 0) {
    fprintf(stderr, "nng_dial(%s): %s\n", url, nng_strerror(r));
    fprintf(stderr, "请先启动 hdic 服务端(./hdic ../config/hdic.conf)\n");
    nng_close(sock);
    return EXIT_FAILURE;
  }

  printf("hdic 指令耗时测试\n");
  printf("  地址    : %s\n", url);
  printf("  迭代次数: %ld (每条指令)\n\n", iter);

  stat_t s1[4], sm[4];
  const char* names[4] = {"add", "set", "get", "del"};
  const char* demo1[4] = {"add k<i>", "set k<i> v<i>", "get k<i>", "del k<i>"};
  const char* demom[4] = {"add a0_<i> ... a31_<i>",
                          "set a0_<i> v0_<i> ... a31_<i> v31_<i>",
                          "get a0_<i> ... a31_<i>", "del a0_<i> ... a31_<i>"};
  for (int i = 0; i < 4; i++) {
    if (stat_init(&s1[i], names[i], demo1[i], iter) != 0 ||
        stat_init(&sm[i], names[i], demom[i], iter) != 0) {
      fprintf(stderr, "内存不足\n");
      nng_close(sock);
      return EXIT_FAILURE;
    }
  }

  char cmd[BUF_SIZE], exp[BUF_SIZE];

  /* 预热: 建连 + 首次分配路径，不计入统计。
     不含 del: 服务端处理 del 时会崩溃，预热阶段不能因此中止 */
  {
    stat_t w;
    if (stat_init(&w, "warm", "", 8) == 0) {
      once(sock, &w, "add warmkey", NULL);
      once(sock, &w, "set warmkey v", NULL);
      once(sock, &w, "get warmkey", NULL);
      if (w.err) {
        fprintf(stderr, "预热失败，服务端未正常应答\n");
        stat_free(&w);
        nng_close(sock);
        return EXIT_FAILURE;
      }
      stat_free(&w);
    }
  }

  /* 分阶段测量: 同一条指令连续跑 iter 次，互不干扰。
     某一阶段连续出错(如服务端崩溃)时跳过其余阶段，已采集的数据照常输出。 */
#define ABORT_IF_DEAD(st)                                   \
  do {                                                      \
    if ((st)->err >= 3) {                                    \
      fprintf(stderr, "服务端无应答，跳过后续阶段\n");        \
      goto out;                                             \
    }                                                       \
  } while (0)

  /* ---- 单键组: 每次迭代一个新 key ---- */
  for (long i = 0; i < iter; i++) {
    snprintf(cmd, sizeof(cmd), "add k%ld", i);
    once(sock, &s1[0], cmd, NULL); /* add 不写 retout，不校验内容 */
  }
  ABORT_IF_DEAD(&s1[0]);
  for (long i = 0; i < iter; i++) {
    snprintf(cmd, sizeof(cmd), "set k%ld v%ld", i, i);
    snprintf(exp, sizeof(exp), "k%ld", i);
    once(sock, &s1[1], cmd, exp);
  }
  ABORT_IF_DEAD(&s1[1]);
  for (long i = 0; i < iter; i++) {
    snprintf(cmd, sizeof(cmd), "get k%ld", i);
    snprintf(exp, sizeof(exp), "k%ld v%ld", i, i);
    once(sock, &s1[2], cmd, exp);
  }
  ABORT_IF_DEAD(&s1[2]);

  /* ---- 多键组: 一条指令 32 个 key ---- */
  for (long i = 0; i < iter; i++) {
    build_keys(cmd, sizeof(cmd), "add", i);
    once(sock, &sm[0], cmd, NULL);
  }
  ABORT_IF_DEAD(&sm[0]);
  for (long i = 0; i < iter; i++) {
    build_pairs(cmd, sizeof(cmd), i);
    build_expect_keys(exp, sizeof(exp), i);
    once(sock, &sm[1], cmd, exp);
  }
  ABORT_IF_DEAD(&sm[1]);
  for (long i = 0; i < iter; i++) {
    build_keys(cmd, sizeof(cmd), "get", i);
    build_expect_pairs(exp, sizeof(exp), i);
    once(sock, &sm[2], cmd, exp);
  }
  ABORT_IF_DEAD(&sm[2]);

  /* ---- del 放最后: 目前会打崩服务端，放前面会影响其它指令的测量 ---- */
  for (long i = 0; i < iter; i++) {
    snprintf(cmd, sizeof(cmd), "del k%ld", i);
    snprintf(exp, sizeof(exp), "k%ld", i);
    once(sock, &s1[3], cmd, exp);
    ABORT_IF_DEAD(&s1[3]);
  }
  for (long i = 0; i < iter; i++) {
    build_keys(cmd, sizeof(cmd), "del", i);
    build_expect_keys(exp, sizeof(exp), i);
    once(sock, &sm[3], cmd, exp);
    ABORT_IF_DEAD(&sm[3]);
  }

out:

  report_head("单键 (每条指令 1 个 key)");
  for (int i = 0; i < 4; i++) report(&s1[i]);
  printf("\n");
  {
    char g[64];
    snprintf(g, sizeof(g), "多键 (每条指令 %d 个 key)", MKEYS);
    report_head(g);
  }
  for (int i = 0; i < 4; i++) report(&sm[i]);
  printf("\n");

  double total = 0;
  long total_n = 0, bad = 0, err = 0;
  for (int i = 0; i < 4; i++) {
    total += s1[i].elapsed + sm[i].elapsed;
    total_n += s1[i].lat_n + sm[i].lat_n;
    bad += s1[i].bad + sm[i].bad;
    err += s1[i].err + sm[i].err;
  }
  printf("[合计] %ld 次往返, 耗时 %.3f s, 平均 %.0f 次/秒, bad=%ld err=%ld\n",
         total_n, total, total > 0 ? (double)total_n / total : 0, bad, err);

  printf("\n各指令示例:\n");
  for (int i = 0; i < 4; i++) printf("  %-3s : %s | %s\n", names[i], demo1[i], demom[i]);

  for (int i = 0; i < 4; i++) {
    stat_free(&s1[i]);
    stat_free(&sm[i]);
  }
  nng_close(sock);
  return (bad || err) ? EXIT_FAILURE : EXIT_SUCCESS;
}
