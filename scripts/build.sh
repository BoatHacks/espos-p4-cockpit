#!/usr/bin/env bash
# Run an ESP-IDF build without starving the interactive session.
#
# A full build saturates all cores on a 4-core Pi and the editor's SSH
# session stops getting scheduled. nice/ionice + one core spare fix that,
# and a lock keeps two builds from racing on build/.
#
# Usage: scripts/build.sh [idf.py args…]     (default: build)
set -euo pipefail

LOCK="${TMPDIR:-/tmp}/.idf-build-$(id -u)-$(basename "$PWD").lock"
exec 9>"$LOCK"
if ! flock -n 9; then
  if [ "${BUILD_NOWAIT:-0}" = "1" ]; then
    echo "==> another build is running (BUILD_NOWAIT=1) — aborting" >&2
    exit 1
  fi
  echo "==> waiting for the running build to finish…" >&2
  flock 9
fi

if [ -z "${IDF_PATH:-}" ]; then
  echo "==> IDF_PATH not set: source the export.sh of ESP-IDF $(cat .idf-version)" >&2
  exit 1
fi

# IDF 6's idf.py has no -j option; parallelism is ninja's. Configure via
# idf.py (component manager, sdkconfig), then compile with a capped ninja.
JOBS="${BUILD_JOBS:-3}"
if [ $# -gt 0 ] && [ "$1" != "build" ]; then
  exec nice -n 15 ionice -c 3 idf.py "$@"
fi
nice -n 15 ionice -c 3 idf.py reconfigure
exec nice -n 15 ionice -c 3 ninja -C build -j "$JOBS"
