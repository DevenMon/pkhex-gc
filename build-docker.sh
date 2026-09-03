#!/usr/bin/env bash
# Thin wrapper kept for compatibility: build.sh does the same thing and also
# works with a locally installed devkitPPC.
set -euo pipefail
exec "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/build.sh" --docker "$@"
