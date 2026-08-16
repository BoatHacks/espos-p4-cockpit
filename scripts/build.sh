#!/usr/bin/env bash
# Run a PlatformIO build without starving the interactive session.
#
# A full ESP-IDF build saturates all cores on this 4-core Pi: load hits ~8,
# and the VS Code server (and the SSH session carrying it) stops getting
# scheduled, so the editor disconnects mid-build. Two knobs fix that:
#
#   nice -n 15   compile work yields to anything interactive
#   -j N-1       leave one core for the editor, shell and network stack
#
# ionice too: the link step and the component manager are IO-heavy, and IO
# starvation stalls the editor just as effectively as CPU starvation.
#
# Usage: scripts/build.sh [-e env] [...pio run args]
set -euo pipefail

jobs=$(( $(nproc) - 1 ))
[ "$jobs" -lt 1 ] && jobs=1

cmd=(pio run -j "$jobs" "$@")
command -v ionice >/dev/null 2>&1 && cmd=(ionice -c3 "${cmd[@]}")

echo "==> nice -n 15, -j $jobs (of $(nproc) cores)"
exec nice -n 15 "${cmd[@]}"
