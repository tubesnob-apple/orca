#!/usr/bin/env bash
#
# run_tests.sh — MakeLib test suite
#
# Tests single and multi-module library builds to validate correctness of the
# MakeLib binary.  Test 2 (multi-module single invocation) specifically
# exercises the known bug where only the OMF dictionary is written when
# multiple +file.a arguments are given in one call.
#
# Usage:
#   ./tests/run_tests.sh            # test the currently installed binary
#   make test                       # same, via make
#
# To test a freshly built binary:
#   make install && make test
#
# IMPORTANT: 'iix makelib' always runs the binary installed in
# /Library/GoldenGate/Utilities/MakeLib, never ./MakeLib directly.
# You MUST run 'make install' before 'make test' when validating a new build.
#
# Exit code: 0 if all tests pass, 1 if any fail.
#

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FIXTURES_DIR="$SCRIPT_DIR/fixtures"
MAKELIB="${MAKELIB:-iix makelib}"
WORK_DIR="$(mktemp -d /tmp/makelib_tests.XXXXXX)"
PASS=0
FAIL=0

trap 'rm -rf "$WORK_DIR"' EXIT

# ── helpers ──────────────────────────────────────────────────────────────────

pass() { echo "  PASS: $1"; PASS=$((PASS + 1)); }
fail() { echo "  FAIL: $1"; FAIL=$((FAIL + 1)); }

# Run MakeLib silently (-P suppresses progress output).
# Extra arguments are passed directly to MakeLib.
run_makelib() {
    $MAKELIB -P "$@" 2>/dev/null
}

# Validate library segment counts via check_library.py.
check() {
    local lib="$1" expected="$2"
    python3 "$SCRIPT_DIR/check_library.py" "$lib" "$expected"
}

# ── setup ────────────────────────────────────────────────────────────────────

echo "Generating fixtures..."
python3 "$SCRIPT_DIR/gen_fixtures.py" "$FIXTURES_DIR"
FIXA="$FIXTURES_DIR/fixa.a"
FIXB="$FIXTURES_DIR/fixb.a"

echo "Binary under test: $MAKELIB"
echo ""

# ── test 1: single module add ─────────────────────────────────────────────────
echo "Test 1: single module add"
LIB="$WORK_DIR/lib1"
if run_makelib "$LIB" "+$FIXA" && check "$LIB" 1; then
    pass "single module add"
else
    fail "single module add"
fi

# ── test 2: two modules in ONE invocation (the bug) ───────────────────────────
echo ""
echo "Test 2: two modules, single invocation  [bug test]"
LIB="$WORK_DIR/lib2"
if run_makelib "$LIB" "+$FIXA" "+$FIXB" && check "$LIB" 2; then
    pass "two modules, single invocation"
else
    fail "two modules, single invocation  ← expected failure with unfixed binary"
fi

# ── test 3: two modules via separate invocations (the known workaround) ───────
echo ""
echo "Test 3: two modules, incremental (separate invocations)"
LIB="$WORK_DIR/lib3"
if run_makelib "$LIB" "+$FIXA" \
    && run_makelib "$LIB" "+$FIXB" \
    && check "$LIB" 2; then
    pass "two modules, incremental"
else
    fail "two modules, incremental"
fi

# ── test 4: add second module to an existing library (tests CopyLib path) ─────
echo ""
echo "Test 4: add module to existing library"
LIB="$WORK_DIR/lib4"
if run_makelib "$LIB" "+$FIXA" \
    && run_makelib "$LIB" "+$FIXB" \
    && check "$LIB" 2; then
    pass "add module to existing library"
else
    fail "add module to existing library"
fi

# ── test 5: delete a module ───────────────────────────────────────────────────
echo ""
echo "Test 5: delete a module"
LIB="$WORK_DIR/lib5"
if run_makelib "$LIB" "+$FIXA" \
    && run_makelib "$LIB" "+$FIXB" \
    && run_makelib "$LIB" "-$FIXA" \
    && check "$LIB" 1; then
    pass "delete a module"
else
    fail "delete a module"
fi

# ── summary ──────────────────────────────────────────────────────────────────
echo ""
TOTAL=$((PASS + FAIL))
echo "Results: $PASS/$TOTAL passed"
[ "$FAIL" -eq 0 ]
