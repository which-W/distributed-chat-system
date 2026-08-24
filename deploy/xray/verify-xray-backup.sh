#!/usr/bin/env bash
set -euo pipefail

CONFIG_PATH="${1:-/usr/local/etc/xray/config.json}"

/usr/local/bin/xray run -test -c "$CONFIG_PATH"
systemctl is-active --quiet xray-backup.service
ss -lnt | grep -qE '(^|[[:space:]])[^[:space:]]*:9443[[:space:]]'
curl --fail --silent --show-error --insecure --max-time 10 https://127.0.0.1/nginx-health >/dev/null

echo "Xray configuration, service, port 9443, and local Gate health check are OK."
