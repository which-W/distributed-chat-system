#!/usr/bin/env sh
set -eu

BUILD_DIR=${1:-"$(pwd)/build/linux-server-release"}
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
BIN_DIR="$BUILD_DIR/bin"
RUN_DIR="$BUILD_DIR/run"
LOG_DIR="$BUILD_DIR/logs"

if [ -f "$PROJECT_DIR/.env" ]; then
    set -a
    # shellcheck disable=SC1091
    . "$PROJECT_DIR/.env"
    set +a
fi

mkdir -p "$RUN_DIR" "$LOG_DIR"

start_cpp_service() {
    name=$1
    config_file=$2
    executable=$3
    pid_file="$RUN_DIR/$name.pid"

    if [ -f "$pid_file" ] && kill -0 "$(cat "$pid_file")" 2>/dev/null; then
        echo "$name is already running (pid $(cat "$pid_file"))"
        return
    fi
    if [ ! -x "$executable" ]; then
        echo "missing executable: $executable" >&2
        exit 1
    fi

    CHAT_CONFIG_FILE="$config_file" nohup "$executable" >"$LOG_DIR/$name.log" 2>&1 &
    echo $! >"$pid_file"
    echo "started $name (pid $!)"
}

start_node_service() {
    name=varify_server
    pid_file="$RUN_DIR/$name.pid"
    if [ -f "$pid_file" ] && kill -0 "$(cat "$pid_file")" 2>/dev/null; then
        echo "$name is already running (pid $(cat "$pid_file"))"
        return
    fi
    if [ ! -d "$PROJECT_DIR/VarifyServer/node_modules" ]; then
        echo "VarifyServer dependencies are missing; run: npm ci --prefix VarifyServer" >&2
        exit 1
    fi
    (
        cd "$PROJECT_DIR/VarifyServer"
        nohup node server.js >"$LOG_DIR/$name.log" 2>&1 &
        echo $! >"$pid_file"
    )
    echo "started $name (pid $(cat "$pid_file"))"
}

start_node_service
start_cpp_service status_server "$PROJECT_DIR/config/status.ini" "$BIN_DIR/status_server"
start_cpp_service chatserver1 "$PROJECT_DIR/config/chatserver1.ini" "$BIN_DIR/chat_server"
start_cpp_service chatserver2 "$PROJECT_DIR/config/chatserver2.ini" "$BIN_DIR/chat_server"
start_cpp_service gate_server "$PROJECT_DIR/config/gate.ini" "$BIN_DIR/gate_server"

echo "logs: $LOG_DIR"
