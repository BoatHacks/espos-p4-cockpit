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

UI_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/espos/ui"
if [ ! -f "$UI_DIR/package.json" ]; then
  echo "==> espos/ submodule is empty — run \`git submodule update --init\`" >&2
  exit 1
fi
cd "$UI_DIR"

# npm ci wipes and reinstalls node_modules every time; only pay for it
# when there is nothing to reuse.
if [ ! -d node_modules ]; then
  nice -n 15 ionice -c 3 npm ci
fi

if [ $# -eq 0 ]; then
  set -- build
fi
exec nice -n 15 ionice -c 3 npm run "$@"
