#!/usr/bin/env sh
set -eu

preset="${1:-linux-server-release}"
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_root"
cmake --fresh --preset "$preset"
cmake --build --preset "$preset"
