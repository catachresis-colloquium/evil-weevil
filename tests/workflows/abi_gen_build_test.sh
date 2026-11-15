#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 Jonathan D.A. Jewell (metadatastician) <j.d.a.jewell@open.ac.uk>
#
# abi_gen_build_test.sh — watch `just abi-gen-build` FAIL when the build fails.
#
#   bash tests/workflows/abi_gen_build_test.sh
#
# Why this exists. idris2 0.7.0 exits 0 after "INTERNAL ERROR: Can't find data
# file chez/support.ss", having built nothing. `abi-gen-build` used to print
# "built" over that, so the step reported success after a failure and only the
# NEXT recipe failed (CI, every run from 2026-10-01). The recipe now checks the
# artefact, not the exit code. A guard nobody has watched fail is not a guard, so
# this test plants the failure and requires the recipe to notice:
#
#   1. PLANT. IDRIS2_PREFIX points at a scratch prefix that links the installed
#      libraries but withholds `support/`: the half-installed layout, and the
#      exact "Can't find data file chez/support.ss" error. (MEASURED on 0.7.0: an
#      empty IDRIS2_DATA alone does NOT reproduce it — idris2 still finds its
#      support files through the prefix and builds — so it is not a valid plant.)
#      The recipe must exit non-zero, must say it produced no executable, and no
#      executable may be left behind. The test also requires idris2 itself to
#      report the internal error and exit 0, so the plant is known to be the
#      failure it claims to be, not some other one.
#   2. CONTROL. The plant removed, the same recipe must exit 0 and the executable
#      must exist. Without this, leg 1 could pass because the recipe always fails.
#
# Exit: 0 = both legs behaved; 1 = a leg did not (or a tool is missing — this never
# skips); 2 = misuse.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$REPO_ROOT"

for tool in idris2 just; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "FAIL: '$tool' is not on PATH (not run, not passed)." >&2
        echo "      Run: bash scripts/install-toolchain.sh" >&2
        exit 1
    fi
done

SCRATCH="$(mktemp -d)"
trap 'rm -rf "$SCRATCH"' EXIT
EXE=_build/idris2/exec/ee-abi-gen

pass=0
fail=0
report() {
    if [ "$1" -eq 0 ]; then echo "  PASS: $2"; pass=$((pass + 1))
    else echo "  FAIL: $2"; fail=$((fail + 1)); fi
}
# expect <description> <command...> — a failing assertion is REPORTED, never a
# silent `set -e` abort (which would also hide which assertion broke).
no_match() { ! grep "$@"; }
expect() {
    local desc="$1"; shift
    if "$@"; then report 0 "$desc"; else report 1 "$desc"; fi
}

# The real prefix is where the idris2 on PATH keeps its libraries.
REAL_PREFIX="$(cd "$(dirname "$(readlink -f "$(command -v idris2)")")/.." && pwd)"
REAL_LIBS="$REAL_PREFIX/idris2-$(idris2 --version | sed -n 's/.*version \([0-9][0-9.]*\).*/\1/p' | head -1)"
if [ ! -d "$REAL_LIBS/support" ]; then
    echo "FAIL: expected $REAL_LIBS/support in the installed prefix (not run, not passed)." >&2
    exit 1
fi
HALF="$SCRATCH/half-prefix"
mkdir -p "$HALF/$(basename "$REAL_LIBS")"
for d in "$REAL_LIBS"/*; do
    [ "$(basename "$d")" = support ] || ln -s "$d" "$HALF/$(basename "$REAL_LIBS")/"
done

echo "==> leg 0: the plant is the failure it claims to be (idris2 exits 0 with the internal error)"
rm -rf "$SCRATCH/probe-build"
rc=0
IDRIS2_PREFIX="$HALF" idris2 --source-dir src/interface --build-dir "$SCRATCH/probe-build" \
    -o ee-abi-gen src/interface/Abi/Gen.idr > "$SCRATCH/probe.log" 2>&1 || rc=$?
expect "idris2 itself exits 0 on the plant (the trap)" test "$rc" -eq 0
expect "idris2 reports the chez/support.ss internal error" grep -q "Can't find data file chez/support.ss" "$SCRATCH/probe.log"
expect "idris2 built nothing" test ! -e "$SCRATCH/probe-build/exec/ee-abi-gen"

echo "==> leg 1: the plant in place — the recipe must fail"
rc=0
IDRIS2_PREFIX="$HALF" just abi-gen-build > "$SCRATCH/plant.log" 2>&1 || rc=$?
sed 's/^/      | /' "$SCRATCH/plant.log" | tail -12
expect "exit status is non-zero (got $rc)" test "$rc" -ne 0
expect "the recipe says idris2 produced no executable" grep -q "produced no" "$SCRATCH/plant.log"
expect "the recipe does not claim 'built'" no_match -q "^    built " "$SCRATCH/plant.log"
expect "no executable is left behind" test ! -e "$EXE"

echo "==> leg 2: the plant removed — the same recipe must pass"
rc=0
just abi-gen-build > "$SCRATCH/control.log" 2>&1 || rc=$?
sed 's/^/      | /' "$SCRATCH/control.log" | tail -4
expect "exit status is zero (got $rc)" test "$rc" -eq 0
expect "the executable exists" test -x "$EXE"

echo ""
echo "abi-gen-build self-test: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
