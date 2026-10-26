#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
header="$root/codemp/game/bg_public.h"
predict="$root/codemp/cgame/cg_predict.c"

rg -q '#define[[:space:]]+EF_FLYING_RESPAWN[[:space:]]+\(1<<12\)' "$header"
! rg -q 'EF_NOT_USED_1' "$header"

valid_line=$(rg -n 'cg\.validPPS = qtrue;' "$predict" | cut -d: -f1)
gate_line=$(rg -n 'if\s*\(\s*cg\.snap->ps\.eFlags & EF_FLYING_RESPAWN\s*\)' "$predict" | cut -d: -f1)
pmove_line=$(rg -n 'cg_pmove\.ps = &cg\.predictedPlayerState;' "$predict" | cut -d: -f1)

test -n "$valid_line"
test -n "$gate_line"
test -n "$pmove_line"
test "$valid_line" -lt "$gate_line"
test "$gate_line" -lt "$pmove_line"
rg -Uq 'if\s*\(\s*cg\.snap->ps\.eFlags & EF_FLYING_RESPAWN\s*\)\s*\{\s*CG_InterpolatePlayerState\( qfalse \);\s*return;\s*\}' "$predict"
