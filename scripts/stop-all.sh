#!/usr/bin/env sh
set -eu

BUILD_DIR=${1:-"$(pwd)/build/linux-server-release"}
RUN_DIR="$BUILD_DIR/run"

for name in gate_server chatserver2 chatserver1 status_server varify_server; do
    pid_file="$RUN_DIR/$name.pid"
    if [ ! -f "$pid_file" ]; then
        continue
    fi
    pid=$(cat "$pid_file")
    if kill -0 "$pid" 2>/dev/null; then
        kill "$pid"
        echo "stopped $name (pid $pid)"
    fi
    rm -f "$pid_file"
done
