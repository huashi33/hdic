#!/bin/bash
# 编译并运行 hdic nng 接口性能测试
#
# 用法:
#   ./run_nng_test.sh [iterations]        # 需自行先启动 hdic
#   ./run_nng_test.sh [iterations] -s     # 由脚本自动启停 hdic
#
# 环境:
#   头文件 /home/huashi/code/include, 库 /home/huashi/code/lib

set -u

HCODE=/home/huashi/code
TESTDIR="$(cd "$(dirname "$0")" && pwd)"
BUILDDIR="$TESTDIR/../build"
ITER="${1:-100000}"
AUTOSTART="${2:-}"

export LD_LIBRARY_PATH="$HCODE/lib"

# ---- 编译 ----
echo "== 编译 nng_test =="
gcc -O2 -Wall -Wextra -o "$TESTDIR/nng_test" "$TESTDIR/nng_test.c" \
    -I"$HCODE/include" -L"$HCODE/lib" -lnng -lpthread || exit 1

# ---- 可选: 自动启动服务端 ----
SRV_PID=""
cleanup() {
  if [ -n "$SRV_PID" ]; then
    echo "== 停止 hdic (pid $SRV_PID) =="
    kill "$SRV_PID" 2>/dev/null
    wait "$SRV_PID" 2>/dev/null
    rm -f /tmp/hdic.sock
  fi
}
trap cleanup EXIT

if [ "$AUTOSTART" = "-s" ]; then
  if [ ! -x "$BUILDDIR/hdic" ]; then
    echo "找不到 $BUILDDIR/hdic，请先 make" >&2
    exit 1
  fi
  rm -f /tmp/hdic.sock
  echo "== 启动 hdic =="
  # hdic 的配置里用的是相对路径，必须在 build 目录下启动
  ( cd "$BUILDDIR" && ./hdic ../config/hdic.conf ) >"$TESTDIR/hdic_stdout.log" 2>&1 &
  SRV_PID=$!
  sleep 1
  if ! kill -0 "$SRV_PID" 2>/dev/null; then
    echo "hdic 启动失败，见 $TESTDIR/hdic_stdout.log" >&2
    SRV_PID=""
    exit 1
  fi
fi

# ---- 运行 ----
echo "== 运行测试 (iterations=$ITER) =="
"$TESTDIR/nng_test" "ipc:///tmp/hdic.sock" "$ITER"
RC=$?

echo "== 退出码 $RC =="
exit $RC
