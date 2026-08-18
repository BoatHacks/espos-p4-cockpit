#!/usr/bin/env bash
# Build the espOS web UI without starving the interactive session.
#
# tsc + vite saturate all cores on a 4-core Pi just like the firmware
# build does, and this one is easy to forget because it lives in the
# submodule. Same nice/ionice treatment as scripts/build.sh.
#
# Output lands in espos/ui/dist-gz/, which CMakeLists.txt picks up as the
# LittleFS "storage" image.
#
# Usage: scripts/build-ui.sh [npm run args…]     (default: build)
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UI_DIR="$REPO_DIR/espos/ui"
if [ ! -f "$UI_DIR/package.json" ]; then
  echo "==> espos/ submodule is empty — run \`git submodule update --init\`" >&2
  exit 1
fi
cd "$UI_DIR"

# gzip-dist.mjs rm -rf's dist-gz/ before repopulating it, so a second run
# overlapping the first leaves CMake packaging a half-empty storage image.
LOCK="${TMPDIR:-/tmp}/.ui-build-$(id -u)-$(basename "$REPO_DIR").lock"
exec 9>"$LOCK"
if ! flock -n 9; then
  echo "==> waiting for the running UI build to finish…" >&2
  flock 9
fi

# npm ci wipes and reinstalls node_modules, so only pay for it when there
# is nothing to reuse — or when the lockfile no longer matches what was
# installed, which would otherwise build against stale packages.
LOCK_HASH="$(sha256sum package-lock.json | cut -d' ' -f1)"
STAMP="node_modules/.espos-lock-hash"
if [ ! -d node_modules ] || [ "$(cat "$STAMP" 2>/dev/null)" != "$LOCK_HASH" ]; then
  nice -n 15 ionice -c 3 npm ci
  printf '%s\n' "$LOCK_HASH" > "$STAMP"
fi

if [ $# -eq 0 ]; then
  set -- build
fi
exec nice -n 15 ionice -c 3 npm run "$@"
