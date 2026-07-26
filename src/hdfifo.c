#include "hdfifo.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define DEFAULT_FIFO_PATH "/tmp/my_fifo"
#define BUF_SIZE 1024

// return fd
static int hdfifo_new(const char* name, int rorw) {
  /* 1. 创建 FIFO(命名管道)。若已存在则忽略 EEXIST 错误。 */
  if (mkfifo(name, 0666) == -1 && errno != EEXIST) {
    fprintf(stderr, "mkfifo(%s) 失败: %s\n", name, strerror(errno));
    return EXIT_FAILURE;
  }

  int fd = open(name, rorw);
  if (fd == -1) {
    fprintf(stderr, "open(%s) 失败: %s\n", name, strerror(errno));
    return EXIT_FAILURE;
  }
  return fd;
}

// void hdproc_dispatch(const char *cmdline,size_t len);
int hdfifo_main(int argc, char* argv[], hdcontext_t* c,
                int (*process)(uint8_t* d, size_t, hdcontext_t* c)) {
  printf("hw from hdfifo\n");

  int fdr = hdfifo_new(argv[2], O_RDWR);
  int fdw = hdfifo_new(argv[3], O_RDWR);

  /* 3. 循环阻塞读取并打印,收到 "quit" 则退出。 */
  char buf[BUF_SIZE];
  for (;;) {
    ssize_t n = read(fdr, buf, sizeof(buf) - 1); /* 无数据时阻塞在这里 */

    if (n > 0) {
      buf[n] = '\0'; /* 补上字符串结束符 */

      /* 去掉末尾的换行符,便于和 "quit" 精确比较 */
      size_t len = (size_t)n;
      while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
        buf[--len] = '\0';

      if (strcmp(buf, "quit") == 0) {
        printf("收到 quit,退出。\n");
        break;
      }
      hbuf_clear(&c->retout);
      c->ret = process(buf, len, c);
      if (c->retout.len) {
        n = write(fdw, c->retout.data, c->retout.len);
        HLOG_DEBUG("> [%zu]%s",c->retout.len, c->retout.data);
      }

    } else if (n == -1) {
      if (errno == EINTR) continue; /* 被信号打断,重试 */
      fprintf(stderr, "read 失败: %s\n", strerror(errno));
      break;
    }
    /* 因为用 O_RDWR 打开,n == 0(EOF)的情况不会出现 */
  }
  close(fdr);
  close(fdw);
  /* 如需删除管道文件可取消下一行注释 */
  /* unlink(fifo_path); */
  return EXIT_SUCCESS;
}
