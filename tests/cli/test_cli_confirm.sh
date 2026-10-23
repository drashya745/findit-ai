#!/usr/bin/env bash
# Menu-level test: drives the real findit binary through stdin (option 10, Confirm a match),
# then restarts it to check that the confirmation persisted. Run by CTest:
#   bash tests/cli/test_cli_confirm.sh <path-to-findit> <source-dir>
set -euo pipefail
FINDIT="$1"
SRC="$2"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

mkdir -p "$WORK/data/processed"
cp "$SRC/evaluation/synthetic_v1/users.csv" "$SRC/evaluation/synthetic_v1/reports.csv" "$WORK/data/processed/"

run() {
  "$FINDIT" --data-dir "$WORK/data" --text-engine keyword --matching-config "$SRC/config/matching.conf"
}
expect() {  # expect <text> <file> <label>
  if grep -qF -- "$1" "$2"; then
    echo "ok   - $3"
  else
    echo "FAIL - $3: expected '$1'"
    echo "----- output -----"
    cat "$2"
    exit 1
  fi
}

# Session 1: confirm one pair, then try every invalid variant.
printf '%s\n' \
  10 R900001 R950001 y \
  10 R900001 R950001 \
  10 R900002 R950001 \
  10 R999999 R950003 \
  10 R950003 R900002 \
  10 R900002 R950003 n \
  5 R900001 \
  0 | run > "$WORK/session1.txt"

expect "Confirmed: R900001 and R950001 are now MATCHED (saved to" "$WORK/session1.txt" "confirm a valid pair"
expect "Error: R900001 is already MATCHED; it cannot be confirmed again." "$WORK/session1.txt" "repeated confirmation rejected"
expect "Error: R950001 is already MATCHED; it cannot be confirmed again." "$WORK/session1.txt" "found item already matched"
expect "Error: Report not found: R999999" "$WORK/session1.txt" "unknown report ID"
expect "Error: enter a LOST report ID first and a FOUND report ID second." "$WORK/session1.txt" "wrong order/types"
expect "Not confirmed; nothing changed." "$WORK/session1.txt" "user declines"
expect "R900001 is already MATCHED; it no longer needs suggestions." "$WORK/session1.txt" "no suggestions for matched report"

LINES=$(wc -l < "$WORK/data/store/matches.csv" | tr -d ' ')
[ "$LINES" = "2" ] && echo "ok   - exactly one confirmation logged" || { echo "FAIL - matches.csv has $LINES lines"; cat "$WORK/data/store/matches.csv"; exit 1; }
cmp -s "$SRC/evaluation/synthetic_v1/reports.csv" "$WORK/data/processed/reports.csv" \
  && echo "ok   - processed reports.csv not modified" || { echo "FAIL - reports.csv was modified"; exit 1; }

# Session 2: restart; statuses must be restored from the log.
printf '%s\n' 4 R900001 4 R950001 4 R900002 0 | run > "$WORK/session2.txt"
expect "and 1 confirmed match(es)" "$WORK/session2.txt" "log replayed on start"
[ "$(grep -c 'Status        : MATCHED' "$WORK/session2.txt")" = "2" ] \
  && echo "ok   - both reports MATCHED after restart" || { echo "FAIL - statuses not restored"; cat "$WORK/session2.txt"; exit 1; }
expect "Status        : OPEN" "$WORK/session2.txt" "unrelated report still OPEN"
echo "All CLI confirmation checks passed."
