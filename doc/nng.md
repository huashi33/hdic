# hdic 中的 nng 使用说明

本机 nng 版本 **1.5**（`/home/huashi/code/include/nng/nng.h` 中 `NNG_MAJOR_VERSION 1` / `NNG_MINOR_VERSION 5`），
配套工具 `nngcat` 版本 1.5.2，位于 `/home/huashi/code/bin/nngcat`。

hdic 使用 **REQ/REP** 模式：hdic 是 REP 端（服务端，listen），客户端是 REQ 端（dial）。

---

## 1. 头文件与链接

nng 的 API 分两层，**核心 API 和协议 API 在不同头文件里**：

| 内容 | 头文件 |
|------|--------|
| `nng_listen` / `nng_dial` / `nng_send` / `nng_recv` / `nng_close` / `nng_free` | `nng/nng.h` |
| `nng_rep0_open`（REP 端构造） | `nng/protocol/reqrep0/rep.h` |
| `nng_req0_open`（REQ 端构造） | `nng/protocol/reqrep0/req.h` |

只包含 `nng/nng.h` 就调用 `nng_rep0_open`，会得到
`warning: implicit declaration of function 'nng_rep0_open'`。gcc 11 只警告，gcc 14 / clang 16+ 直接报错。

本工程在 `include/hddef.h` 中统一包含：

```c
#include "nng/nng.h"
#include "nng/protocol/reqrep0/rep.h"
```

CMake 侧（`CMakeLists.txt`）：

```cmake
include_directories(/home/huashi/code/include)
link_directories(/home/huashi/code/lib)
target_link_libraries(hdic PUBLIC zlog iniparser hbase nng)
```

运行期需要能找到共享库：

```bash
export LD_LIBRARY_PATH=/home/huashi/code/lib
```

---

## 2. URL 格式

nng 用 URL 同时表达传输方式和地址。**scheme 写错不会编译报错，只会在 `nng_listen` / `nng_dial` 时返回错误码。**

| scheme | 用途 | 地址形式 | 示例 |
|--------|------|----------|------|
| `tcp://` | 跨主机 TCP | `host:port` | `tcp://0.0.0.0:12244` |
| `ipc://` | 同主机，POSIX 下即 Unix 域套接字 | **文件路径** | `ipc:///tmp/hdic.sock` |
| `unix://` | POSIX 上是 `ipc://` 的别名，可互换 | 文件路径 | `unix:///tmp/hdic.sock` |
| `abstract://` | Linux 抽象命名空间，不落文件系统 | URI 编码的名字 | `abstract://hdic` |
| `inproc://` | 同进程内线程间 | 任意名字 | `inproc://hdic` |
| `tls+tcp://` | 加密 TCP | `host:port` | `tls+tcp://0.0.0.0:12244` |

要点：

- **没有 `uds://` 这个 scheme**。想用 Unix 域套接字请写 `ipc://` 或 `unix://`。
- `ipc://` 后面跟的是路径，不是 `host:port`。写成 `ipc://0.0.0.0:12244`
  会在**当前工作目录**下创建一个名为 `0.0.0.0:12244` 的 socket 文件 —— 语法上能跑通，
  但配置里的 port 实际没起到端口的作用，容易误解，不建议。
- `ipc://` 用相对路径时按 cwd 解析，两个进程 cwd 不同就连不上。**建议一律用绝对路径。**
- `0.0.0.0` 只对 listen 有意义（监听所有网卡），客户端 dial 要写具体地址如 `127.0.0.1`。
- 客户端和服务端的 URL 必须完全一致，包括 scheme。

按 `config/hdic.conf` 里的 `[net] port` 拼 URL 的正确写法：

```c
char url[256] = {0};
// TCP
snprintf(url, sizeof(url), "tcp://0.0.0.0:%d", ctx->cfg.port);
// 或 IPC（port 无意义，用固定路径）
snprintf(url, sizeof(url), "ipc:///tmp/hdic.sock");
```

注意 `snprintf` 第二个参数是含终止 NUL 的缓冲区**总大小**，函数自己保证以 NUL 结尾，
不需要写 `sizeof(url) - 1`。

---

## 3. 服务端（REP）骨架

对应 `src/main.c` 的 `hdic_exec()`：

```c
r = nng_rep0_open(&ctx->sock);
HC_EXEC_RET_WHEN(r, HLOG_ERROR("[%d]nng_rep0_open", r), r);

r = nng_listen(ctx->sock, url, NULL, 0);
HC_EXEC_RET_WHEN(r, HLOG_ERROR("[%d]nng_listen", r); nng_close(ctx->sock), r);

char*  request = NULL;
size_t sz;
while (1) {
  // recv：NNG_FLAG_ALLOC 让 nng 分配缓冲区，用完必须 nng_free
  if ((r = nng_recv(ctx->sock, &request, &sz, NNG_FLAG_ALLOC)) != 0) {
    HLOG_ERROR("[%d]nng_recv", r);
    break;
  }

  // process
  // ...

  nng_free(request, sz);

  // send：REP 协议必须一问一答，收一条就回一条
  if ((r = nng_send(ctx->sock, response, resp_len, 0)) != 0) {
    HLOG_ERROR("[%d]nng_send", r);
    break;
  }
}
nng_close(ctx->sock);
```

约束与注意事项：

- **REP 是严格的一问一答**。收到请求后必须回一条才能收下一条；
  某个分支直接 return 而没有 `nng_send`，客户端会一直等。
- `nng_recv` 配 `NNG_FLAG_ALLOC` 时，`request` 由 nng 分配，必须 `nng_free(request, sz)`，否则泄漏。
- `nng_recv` 给出的数据**不保证以 NUL 结尾**，长度以 `sz` 为准。
  hdic 的 `hdic_parse(uint8_t* cmdline, size_t s, ...)` 按长度扫描，正好匹配这个约定。
  但若要把它当 C 字符串用（如 `%s` 打印），需自己补 NUL 或用 `%.*s`。
- `nng_send` 的长度自己指定。`strlen(response) + 1` 会把结尾的 `\0` 也发给对端，
  客户端会多收到一个 NUL 字节；一般应写 `strlen(response)`。
- nng 消息本身带长度，**不存在 FIFO 那种粘包/半包问题**，一次 `nng_recv` 就是完整一帧。
- nng 的返回值是自己的 errno 体系（0 为成功），和 hbase 的 `HC_RET_*` 不是一套编码，
  混在同一个 `int r` 里返回时要注意区分来源。
- 避免 `if (r = nng_recv(...))` 这种写法，gcc 会给 `-Wparentheses` 警告，
  写成 `if ((r = nng_recv(...)) != 0)`。

---

## 4. 用 nngcat 测试

`nngcat` 不在 PATH 里，先设置环境：

```bash
export LD_LIBRARY_PATH=/home/huashi/code/lib
export PATH=$PATH:/home/huashi/code/bin
```

hdic 是 REP + listen，所以 nngcat 用 `--req` + `--dial`：

```bash
# 发一条命令并打印应答
nngcat --req --dial tcp://127.0.0.1:12244 --data "get name" --ascii
```

常用变体：

```bash
# 从标准输入取数据（含特殊字符或多行时更方便）
echo -n "set name huashi" | nngcat --req --dial tcp://127.0.0.1:12244 --file - --ascii

# 从文件取数据
nngcat --req --dial tcp://127.0.0.1:12244 --file cmd.txt --ascii

# 服务端用 ipc:// 时，-x 即 --connect-ipc
nngcat --req -x /tmp/hdic.sock --data "get name" --ascii

# 应答含 NUL 等不可打印字节时，看清真实内容
nngcat --req --dial tcp://127.0.0.1:12244 --data "get name" --quoted
nngcat --req --dial tcp://127.0.0.1:12244 --data "get name" --hex

# 服务端不回复时避免一直挂着
nngcat --req --dial tcp://127.0.0.1:12244 --data "get name" --ascii --recv-timeout 3

# 看连接建立过程，排查连不上的问题
nngcat --req --dial tcp://127.0.0.1:12244 --data "get name" --ascii -v
```

主要参数：

| 参数 | 说明 |
|------|------|
| `--req` / `--rep` / `--pub` / `--sub` ... | 选协议，必须与对端配对 |
| `--dial <url>` | 主动连接（对端是 listen 方） |
| `--listen <url>` | 监听（对端是 dial 方） |
| `-x <path>` / `-X <path>` | connect-ipc / bind-ipc，直接给路径 |
| `--data <data>` / `-D` | 直接给要发的字节 |
| `--file <file>` / `-F` | 从文件读，`-` 表示标准输入 |
| `--ascii` / `--quoted` / `--hex` / `--raw` | 应答输出格式 |
| `--recv-timeout <secs>` | 接收超时，避免卡死 |
| `--count <num>` / `--interval <secs>` | 发送次数 / 间隔，可做简单压测 |
| `-v` / `-q` | 详细 / 静默 |

`--data` 传的是纯字节，**不会自动补换行或 NUL**。

---

## 5. 排错

| 现象 | 原因 |
|------|------|
| 编译告警 `implicit declaration of 'nng_rep0_open'` | 少包含 `nng/protocol/reqrep0/rep.h` |
| `nng_listen` 返回非 0 | URL scheme 不存在（如 `uds://`）或地址形式与 scheme 不匹配 |
| 客户端连不上 | 服务端未启动、URL 不一致、或 ipc 相对路径受 cwd 影响 |
| nngcat 一直挂着不返回 | 服务端某分支没有 `nng_send`；加 `--recv-timeout` 定位 |
| 应答尾部多一个不可见字节 | 服务端用了 `strlen(x) + 1` 作为发送长度 |
| 运行时 `cannot open shared object libnng.so` | 忘了 `export LD_LIBRARY_PATH=/home/huashi/code/lib` |

---

## 6. 安全提示

`tcp://` 传输**没有任何认证和加密**。绑定 `0.0.0.0` 意味着接受来自任意网卡的连接，
任何能连到该端口的人都可以对 hdic 的字典执行 `set` / `del`。

- 仅本机使用：绑 `tcp://127.0.0.1:port`，或改用 `ipc://` 并通过
  `NNG_OPT_IPC_PERMISSIONS` 收紧 socket 文件权限（默认通常是 0644）。
- 需要跨主机且数据敏感：使用 `tls+tcp://`，并配置证书。

---

## 参考

- nng 官方手册：https://nng.nanomsg.org/man/v1.5.2/
- ipc 传输说明（scheme、别名、路径规则）：`man 7 nng_ipc`
- 本机头文件：`/home/huashi/code/include/nng/`
