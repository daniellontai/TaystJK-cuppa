#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
active="$root/codemp/game/g_active.c"

# A missing set, a stale clear, or a clear after GO must fail this harness.
set_count=$(rg -c 'ps\.eFlags \|= EF_FLYING_RESPAWN;' "$active" || true)
clear_count=$(rg -c 'ps\.eFlags &= ~EF_FLYING_RESPAWN;' "$active" || true)
test "${set_count:-0}" -ge 3
test "${clear_count:-0}" -ge 3

go_line=$(rg -n 'cp \\"\^2GO!\\"' "$active" | cut -d: -f1)
advance_clear=$(rg -n 'ps\.eFlags &= ~EF_FLYING_RESPAWN;' "$active" | awk -F: -v go="$go_line" '$1 < go { line=$1 } END { print line }')

test -n "$go_line"
test -n "$advance_clear"
test "$advance_clear" -lt "$go_line"
