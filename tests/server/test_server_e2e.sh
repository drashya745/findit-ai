#!/usr/bin/env bash
# End-to-end test of findit_server over real HTTP on 127.0.0.1. Run by CTest:
#   bash tests/server/test_server_e2e.sh <findit_server> <findit> <source-dir> <python-for-json>
# Uses a temporary copy of the synthetic evaluation set; never touches the project's data/.
set -uo pipefail
SERVER="$1"; CLI="$2"; SRC="$3"; PY="$4"
[ -x "$PY" ] || PY="$(command -v python3)"
WORK="$(mktemp -d)"
DATA="$WORK/data"
SERVER_PID=""
cleanup() {
  [ -n "$SERVER_PID" ] && kill "$SERVER_PID" 2>/dev/null
  rm -rf "$WORK"
}
trap cleanup EXIT
mkdir -p "$DATA/processed"
cp "$SRC/evaluation/synthetic_v1/users.csv" "$SRC/evaluation/synthetic_v1/reports.csv" "$DATA/processed/"
PROJECT_STORE_BEFORE="$(ls -A "$SRC/data/store" 2>/dev/null | sort | tr '\n' ' ')"

ok()   { echo "ok   - $1"; }
fail() { echo "FAIL - $1"; echo "----- server log -----"; cat "$WORK/server.err" 2>/dev/null; exit 1; }
check(){ if eval "$1"; then ok "$2"; else fail "$2 (check: $1)"; fi; }
# json <file> <python expression on d>: prints the value.
json() { "$PY" -c "import json,sys; d=json.load(open(sys.argv[1])); print($2)" "$1"; }

start_server() {  # start_server [extra server arguments...]
  : > "$WORK/server.out"
  (cd "$SRC" && exec "$SERVER" --port 0 --data-dir "$DATA" "$@" > "$WORK/server.out" 2> "$WORK/server.err") &
  SERVER_PID=$!
  for _ in $(seq 1 100); do
    grep -q "listening on" "$WORK/server.out" 2>/dev/null && break
    kill -0 "$SERVER_PID" 2>/dev/null || fail "server exited during start-up"
    sleep 0.1
  done
  PORT="$(grep -oE '127\.0\.0\.1:[0-9]+' "$WORK/server.out" | head -1 | cut -d: -f2)"
  [ -n "$PORT" ] || fail "server did not report a port"
  BASE="http://127.0.0.1:$PORT"
}
stop_server() {  # stop_server SIGNAL
  kill "-$1" "$SERVER_PID"
  for _ in $(seq 1 100); do kill -0 "$SERVER_PID" 2>/dev/null || break; sleep 0.1; done
  kill -0 "$SERVER_PID" 2>/dev/null && fail "server did not stop on SIG$1"
  wait "$SERVER_PID"; local code=$?
  SERVER_PID=""
  [ "$code" = 0 ] || fail "server exit code $code after SIG$1"
}
# req METHOD PATH [BODY] [CONTENT-TYPE] -> prints HTTP status; body in $WORK/body.json
req() {
  local args=(-s -o "$WORK/body.json" -w "%{http_code}" -X "$1")
  [ $# -ge 3 ] && args+=(-H "Content-Type: ${4:-application/json}" --data-binary "$3")
  curl "${args[@]}" "$BASE$2"
}

# ---------------------------------------------------------------- session 1
start_server
ok "server started on 127.0.0.1:$PORT"
S=$(req GET /api/health); check '[ "$S" = 200 ]' "GET /api/health"
check '[ "$(json "$WORK/body.json" "d[\"matcher\"][\"textEngine\"]")" = python ]' "default config uses the Python text engine"
check '! grep -q "$WORK" "$WORK/body.json"' "health exposes no local paths"
check '[ "$(curl -s -o /dev/null -w "%{http_code}" -H "Host: evil.example" "$BASE/api/health")" = 403 ]' "foreign Host header rejected (DNS rebinding)"

# The data-directory lock blocks a second server and the CLI.
(cd "$SRC" && "$SERVER" --port 0 --data-dir "$DATA" > "$WORK/second.out" 2>&1); second=$?
check '[ "$second" = 3 ] && grep -q "in use by another FindIt process" "$WORK/second.out"' "second server refused (exit 3, owner named)"
check 'grep -q "program=findit_server" "$WORK/second.out"' "lock message identifies findit_server"
(cd "$SRC" && printf '0\n' | "$CLI" --data-dir "$DATA" --text-engine keyword > "$WORK/cli.out" 2>&1); cli=$?
check '[ "$cli" = 1 ] && grep -q "in use by another FindIt process" "$WORK/cli.out"' "CLI refused while the server holds the lock"

S=$(req POST /api/users '{"name":"E2E Student","contact":"e2e@example.edu"}'); check '[ "$S" = 201 ]' "POST /api/users"
USER_ID="$(json "$WORK/body.json" 'd["id"]')"
check '[ "$USER_ID" = U005 ]' "new user is U005"
LOST_BODY="{\"type\":\"lost\",\"reporterId\":\"$USER_ID\",\"category\":\"Wallet\",\"description\":\"Black leather wallet with a student ID card\",\"color\":\"black\",\"location\":\"Library\",\"date\":\"2026-03-20\"}"
S=$(req POST /api/reports "$LOST_BODY"); check '[ "$S" = 201 ]' "POST /api/reports (lost)"
LOST_ID="$(json "$WORK/body.json" 'd["id"]')"
check '[ "$LOST_ID" = R950033 ] && [ "$(json "$WORK/body.json" "d[\"persisted\"]")" = True ]' "report R950033 created and persisted"

S=$(req POST /api/reports '{"type":"lost","reporterId":"U001","category":"Wallet","description":"short","color":"black","location":"Library","date":"2026-03-20"}'); check '[ "$S" = 400 ]' "invalid report rejected (400)"
check '[ "$(json "$WORK/body.json" "d[\"error\"][\"code\"]")" = validation_failed ]' "error body has a code"
S=$(req POST /api/users '{"name":"X Y"}' text/plain); check '[ "$S" = 415 ]' "wrong content type rejected (415)"
BIG="{\"name\":\"$(head -c 20000 /dev/zero | tr '\0' 'a')\"}"
S=$(req POST /api/users "$BIG"); check '[ "$S" = 413 ]' "oversized body rejected (413)"
S=$(req POST /api/users "{not json"); check '[ "$S" = 400 ]' "malformed JSON rejected (400)"
S=$(req PUT /api/reports/R900001); check '[ "$S" = 405 ]' "unsupported method rejected (405)"
S=$(req GET /api/../../etc/passwd); check '[ "$S" = 404 ]' "no file serving (404)"

S=$(req GET "/api/reports/$LOST_ID/matches?k=3"); check '[ "$S" = 200 ]' "GET matches"
check '[ "$(json "$WORK/body.json" "len(d[\"matches\"])")" = 3 ]' "three matches returned"
check '[ "$(json "$WORK/body.json" "d[\"matches\"][0][\"rank\"]")" = 1 ]' "ranked from 1"
check '"$PY" -c "import json,sys; d=json.load(open(sys.argv[1])); s=[m[\"score\"] for m in d[\"matches\"]]; assert s==sorted(s, reverse=True) and all(0<=x<=1 for x in s)" "$WORK/body.json"' "scores in [0,1], descending"
check 'json "$WORK/body.json" "d[\"scoreNote\"]" | grep -q "not probabilities"' "scores labelled as not probabilities"
TOP="$(json "$WORK/body.json" 'd["matches"][0]["found"]["id"]')"
check '[ "$TOP" = R950001 ]' "top match is the black leather wallet R950001"

CONFIRM_BODY="{\"lostId\":\"$LOST_ID\",\"foundId\":\"$TOP\"}"; S=$(req POST /api/matches "$CONFIRM_BODY"); check '[ "$S" = 200 ]' "POST /api/matches (confirm)"
check '[ "$(json "$WORK/body.json" "d[\"persisted\"]")" = True ] && [ "$(json "$WORK/body.json" "d[\"lost\"][\"status\"]")" = matched ]' "confirmation persisted, status matched"
check 'grep -q "^$LOST_ID,$TOP," "$DATA/store/matches.csv"' "matches.csv has the confirmation"
CONFIRM_BODY="{\"lostId\":\"$LOST_ID\",\"foundId\":\"$TOP\"}"; S=$(req POST /api/matches "$CONFIRM_BODY"); check '[ "$S" = 409 ]' "repeated confirmation rejected (409)"
S=$(req GET /api/stats); check '[ "$S" = 200 ] && [ "$(json "$WORK/body.json" "d[\"confirmedMatches\"]")" = 1 ]' "stats: 1 confirmed match"

# Concurrent writes are serialized: 10 parallel registrations -> 10 distinct IDs, 10 rows.
for i in $(seq 1 10); do
  curl -s -o "$WORK/concurrent_$i.json" -X POST -H "Content-Type: application/json" \
       --data-binary "{\"name\":\"Parallel User $i\"}" "$BASE/api/users" &
done
wait_ids="$(for i in $(seq 1 10); do while [ ! -s "$WORK/concurrent_$i.json" ]; do sleep 0.05; done; json "$WORK/concurrent_$i.json" 'd["id"]'; done | sort -u | wc -l | tr -d ' ')"
check '[ "$wait_ids" = 10 ]' "10 concurrent registrations got 10 distinct IDs"
check '[ "$(grep -c "Parallel User" "$DATA/store/users.csv")" = 10 ]' "all 10 concurrent registrations saved, none interleaved"

stop_server TERM
ok "SIGTERM: clean shutdown, exit code 0"
check 'grep -q "releasing the data-directory lock" "$WORK/server.err"' "shutdown released the lock"
(cd "$SRC" && printf '0\n' | "$CLI" --data-dir "$DATA" --text-engine keyword > "$WORK/cli2.out" 2>&1); cli=$?
check '[ "$cli" = 0 ] && grep -q "and 1 confirmed match(es)" "$WORK/cli2.out"' "CLI can use the data directory after shutdown and sees the confirmation"

# ---------------------------------------------------------------- session 2 (restart)
start_server --allow-origin http://localhost:5173
ok "server restarted (with --allow-origin http://localhost:5173)"
ACAO="$(curl -s -D - -o /dev/null -H "Origin: http://localhost:5173" "$BASE/api/health" | grep -i '^access-control-allow-origin' | tr -d '\r')"
check '[ "$ACAO" = "Access-Control-Allow-Origin: http://localhost:5173" ]' "CORS: allowed origin is echoed exactly"
check '! curl -s -D - -o /dev/null -H "Origin: http://evil.example" "$BASE/api/health" | grep -qi "^access-control-allow-origin"' "CORS: other origins get no CORS header"
check '[ "$(curl -s -o /dev/null -w "%{http_code}" -X OPTIONS -H "Origin: http://localhost:5173" -H "Access-Control-Request-Method: POST" "$BASE/api/matches")" = 204 ]' "CORS preflight allowed for the configured origin"
check '[ "$(curl -s -o /dev/null -w "%{http_code}" -X OPTIONS -H "Origin: http://evil.example" "$BASE/api/matches")" = 403 ]' "CORS preflight refused for other origins"
S=$(req GET "/api/reports/$LOST_ID"); check '[ "$S" = 200 ] && [ "$(json "$WORK/body.json" "d[\"status\"]")" = matched ]' "lost report still MATCHED after restart"
S=$(req GET "/api/reports/$TOP"); check '[ "$S" = 200 ] && [ "$(json "$WORK/body.json" "d[\"status\"]")" = matched ]' "found report still MATCHED after restart"
S=$(req GET /api/users); check '[ "$S" = 200 ] && json "$WORK/body.json" "[u[\"id\"] for u in d[\"items\"]]" | grep -q "$USER_ID"' "registered user persisted"
S=$(req GET /api/stats); check '[ "$S" = 200 ] && [ "$(json "$WORK/body.json" "d[\"confirmedMatches\"]")" = 1 ] && [ "$(json "$WORK/body.json" "d[\"reportsCreatedInApp\"]")" = 1 ]' "stats restored after restart"
S=$(req GET "/api/reports/$LOST_ID/matches"); check '[ "$S" = 409 ]' "matched report gets no more suggestions (409)"
stop_server INT
ok "SIGINT: clean shutdown, exit code 0"

check '[ -f "$DATA/store/python_matcher.log" ]' "Python log written inside the custom data directory"
PROJECT_STORE_AFTER="$(ls -A "$SRC/data/store" 2>/dev/null | sort | tr '\n' ' ')"
check '[ "$PROJECT_STORE_BEFORE" = "$PROJECT_STORE_AFTER" ]' "project data/store untouched"
echo "All server end-to-end checks passed."
