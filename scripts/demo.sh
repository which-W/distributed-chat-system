#!/usr/bin/env sh
set -eu

keep=0
if [ "${1:-}" = "--keep" ]; then keep=1; shift; fi
if [ "$#" -ne 0 ]; then
  echo "usage: scripts/demo.sh [--keep]" >&2
  exit 2
fi

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
state="$root/build/demo"
reports="$state/reports"
env_file="$state/demo.env"
mkdir -p "$reports"

random_hex() {
  # od is part of POSIX-friendly coreutils and avoids committing demo secrets.
  od -An -N "$1" -tx1 /dev/urandom | tr -d ' \n'
}

cat >"$env_file" <<EOF
DEMO_REDIS_PASSWORD=$(random_hex 24)
DEMO_MYSQL_PASSWORD=$(random_hex 24)
DEMO_MYSQL_ROOT_PASSWORD=$(random_hex 24)
DEMO_GATE_STATUS_TOKEN=$(random_hex 32)
DEMO_GATE_VARIFY_TOKEN=$(random_hex 32)
DEMO_PEER_TOKEN=$(random_hex 32)
DEMO_FILE_KEY=$(random_hex 32)
DEMO_REPORT_DIR=$reports
E2E_GIT_COMMIT=$(git -C "$root" rev-parse HEAD 2>/dev/null || echo unknown)
EOF
chmod 600 "$env_file"

compose() {
  docker compose --project-directory "$root" --env-file "$env_file" -f "$root/compose.demo.yaml" "$@"
}

ok=0
cleanup() {
  status=$?
  if [ "$status" -eq 0 ]; then ok=1; fi
  if [ "$keep" -eq 1 ] && [ "$ok" -eq 1 ]; then
    echo "Demo is running. Inspect Mailpit at http://127.0.0.1:8025 and reports in $reports"
  elif [ "$ok" -eq 1 ]; then
    compose down --volumes --remove-orphans
  else
    echo "Demo failed; containers and volumes were retained." >&2
    echo "Reports: $reports" >&2
    compose logs --no-color --tail 200 gate status chatserver1 chatserver2 varify >&2 || true
    echo "Clean up with: docker compose --env-file '$env_file' -f '$root/compose.demo.yaml' down -v" >&2
  fi
  exit "$status"
}
trap cleanup EXIT INT TERM

compose up -d --build --wait gate mailpit
compose --profile tools run --rm -e "E2E_GIT_COMMIT=$(git -C "$root" rev-parse HEAD 2>/dev/null || echo unknown)" e2e
echo "End-to-end demo passed. Reports: $reports"
