#include "hdfifo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>


#define DEFAULT_FIFO_PATH "/tmp/my_fifo"
#define BUF_SIZE 1024

// void hdproc_dispatch(const char *cmdline,size_t len);
int hdfifo_main(int argc, char *argv[],hdcontext_t *c,int (*process)(uint8_t* d,size_t,hdcontext_t *c)){
  printf("hw from hdfifo\n");
    const char *fifo_path = (argc > 1) ? argv[1] : DEFAULT_FIFO_PATH;

    /* 1. 创建 FIFO(命名管道)。若已存在则忽略 EEXIST 错误。 */
    if (mkfifo(fifo_path, 0666) == -1 && errno != EEXIST) {
        fprintf(stderr, "mkfifo(%s) 失败: %s\n", fifo_path, strerror(errno));
        return EXIT_FAILURE;
    }

    printf("等待数据写入命名管道: %s\n", fifo_path);
    printf("提示: 在另一个终端执行  echo \"hello\" > %s\n", fifo_path);
    printf("      发送  echo \"quit\" > %s  可让本程序退出\n\n", fifo_path);

    /*
     * 2. 以读写方式(O_RDWR)打开 FIFO,只打开这一次。
     *    这里用 O_RDWR 而不是 O_RDONLY 是关键:
     *    - O_RDONLY 时,一旦所有写端关闭,read 会立即返回 0(EOF),
     *      循环就会不停地拿到 EOF,无法持续阻塞等待。
     *    - O_RDWR 让本进程自身也持有一个写端,管道永远不会"没有写者",
     *      因此在没有数据时 read 会正常阻塞,直到有人写入。
     */
    int fd = open(fifo_path, O_RDWR);
    if (fd == -1) {
        fprintf(stderr, "open(%s) 失败: %s\n", fifo_path, strerror(errno));
        return EXIT_FAILURE;
    }

    /* 3. 循环阻塞读取并打印,收到 "quit" 则退出。 */
    char buf[BUF_SIZE];
    for (;;) {
        ssize_t n = read(fd, buf, sizeof(buf) - 1);   /* 无数据时阻塞在这里 */

        if (n > 0) {
            buf[n] = '\0';                 /* 补上字符串结束符 */

            /* 去掉末尾的换行符,便于和 "quit" 精确比较 */
            size_t len = (size_t)n;
            while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
                buf[--len] = '\0';

            if (strcmp(buf, "quit") == 0) {
                printf("收到 quit,退出。\n");
                break;
            }

            int r = process(buf,len,c);
            if(c->retout.len){
                fprintf(stdout, "replay: %s\n", c->retout.data);
            }

        } else if (n == -1) {
            if (errno == EINTR)
                continue;                  /* 被信号打断,重试 */
            fprintf(stderr, "read 失败: %s\n", strerror(errno));
            break;
        }
        /* 因为用 O_RDWR 打开,n == 0(EOF)的情况不会出现 */
    }

    close(fd);
    /* 如需删除管道文件可取消下一行注释 */
    /* unlink(fifo_path); */
    return EXIT_SUCCESS;
}
