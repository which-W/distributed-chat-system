#!/usr/bin/env bash
set -euo pipefail

EXPECTED_VERSION="26.6.27"

if [[ $# -ne 3 ]]; then
  echo "Usage: sudo $0 /path/to/xray /path/to/server-config.json TRUSTED_SHA256" >&2
  exit 2
fi

XRAY_SOURCE="$1"
CONFIG_SOURCE="$2"
TRUSTED_SHA256="$3"
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

if [[ ! -x "$XRAY_SOURCE" ]]; then
  echo "Xray binary is missing or not executable: $XRAY_SOURCE" >&2
  exit 1
fi
if [[ ! -f "$CONFIG_SOURCE" ]]; then
  echo "Configuration file is missing: $CONFIG_SOURCE" >&2
  exit 1
fi
if [[ ! "$TRUSTED_SHA256" =~ ^[0-9a-fA-F]{64}$ ]]; then
  echo "Trusted SHA-256 must be exactly 64 hexadecimal characters." >&2
  exit 1
fi

# 可信摘要必须来自独立审阅的发布签名/摘要渠道；校验前绝不执行候选文件。
ACTUAL_SHA256="$(sha256sum -- "$XRAY_SOURCE" | awk '{print $1}')"
if [[ "${ACTUAL_SHA256,,}" != "${TRUSTED_SHA256,,}" ]]; then
  echo "Xray SHA-256 mismatch; refusing to execute or install it." >&2
  exit 1
fi

if ! getent group xray >/dev/null; then
  groupadd --system xray
fi
if ! id xray >/dev/null 2>&1; then
  useradd --system --gid xray --home-dir /nonexistent --shell /usr/sbin/nologin xray
fi

install -D -m 0755 "$XRAY_SOURCE" /usr/local/bin/xray
install -d -m 0750 -o root -g xray /usr/local/etc/xray
install -m 0640 -o root -g xray "$CONFIG_SOURCE" /usr/local/etc/xray/config.json
install -D -m 0644 "$SCRIPT_DIR/xray-backup.service" /etc/systemd/system/xray-backup.service

if ! /usr/local/bin/xray version | grep -q "Xray ${EXPECTED_VERSION}"; then
  echo "Verified binary is not Xray ${EXPECTED_VERSION}." >&2
  exit 1
fi

/usr/local/bin/xray run -test -c /usr/local/etc/xray/config.json
systemctl daemon-reload
systemctl enable --now xray-backup.service
systemctl --no-pager --full status xray-backup.service
