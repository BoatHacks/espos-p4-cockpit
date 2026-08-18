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
CORES=$(nproc)
JOBS=$(( CORES > 1 ? CORES - 1 : 1 ))
exec nice -n 15 ionice -c 3 idf.py -j "$JOBS" "${@:-build}"
