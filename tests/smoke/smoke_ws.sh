#!/usr/bin/env bash
# Aevrix Phase 23 WebSocket live smoke test (ws:// + wss://).
#
# Starts the server with tests/smoke/ws.conf (plaintext :18081), runs the
# raw-socket RFC 6455 client (tests/smoke/ws_client.py) through every case,
# then restarts with TLS enabled (:18081 + :18443) and re-runs the TLS-safe
# subset over wss://. Prints RESULT: N passed, M failed and exits non-zero
# on any failure.
#
# Usage:
#   ./smoke_ws.sh                 # run everything
#   SMOKE_OUT=/tmp/x ./smoke_ws.sh  # custom scratch dir for logs/certs
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
OUT="${SMOKE_OUT:-/tmp/aevrix-ws-smoke}"
mkdir -p "$OUT"
cd "$REPO"

PASS=0
FAIL=0
SV=""

stop_server() {
    if [ -n "${SV:-}" ]; then
        kill -TERM "$SV" 2>/dev/null
        sleep 0.4
        kill -KILL "$SV" 2>/dev/null
        wait 2>/dev/null
        SV=""
    fi
    # Belt and suspenders: no stray server may hold the smoke ports.
    pkill -f "build/aevrix -c " 2>/dev/null
    sleep 0.3
}
trap stop_server EXIT

run_case() { # <name> [extra client args...]
    local name="$1"; shift
    local detail
    if detail="$(python3 "$HERE/ws_client.py" "$name" "$@" 2>&1)"; then
        echo "PASS $name${1:+ ($*)}: $detail"
        PASS=$((PASS + 1))
    else
        echo "FAIL $name${1:+ ($*)}: $detail"
        FAIL=$((FAIL + 1))
    fi
}

echo "=== Phase 23 ws:// smoke (plaintext :18081) ==="
./build/aevrix -c "$HERE/ws.conf" > "$OUT"/aevrix_ws.log 2>&1 &
SV=$!
sleep 1.2
if ! curl -sS --max-time 5 -o /dev/null -w 'static=%{http_code}\n' http://127.0.0.1:18081/index.html; then
    echo "FAIL: server did not answer plaintext HTTP on :18081"
    FAIL=$((FAIL + 1))
fi
for c in handshake echo_text echo_binary echo_16bit fragmented ping close \
         invalid_unmasked invalid_rsv oversize badkey badversion badconn \
         badorigin noorigin plain_get idle_timeout static_regression; do
    run_case "$c"
done
stop_server

echo
echo "=== Phase 23 wss:// smoke (TLS :18443) ==="
if [ ! -f "$OUT/ws_cert.pem" ]; then
    openssl req -x509 -newkey rsa:2048 -keyout "$OUT/ws_key.pem" \
        -out "$OUT/ws_cert.pem" -days 2 -nodes -subj "/CN=127.0.0.1" >/dev/null 2>&1
fi
{
    echo "# Generated: plaintext :18081 + TLS :18443 for the wss smoke leg"
    grep -v "^tls_enabled\|^tls_cert_file\|^tls_key_file\|^tls_port" "$HERE/ws.conf"
    echo "tls_enabled = true"
    echo "tls_cert_file = $OUT/ws_cert.pem"
    echo "tls_key_file = $OUT/ws_key.pem"
    echo "tls_port = 18443"
} > "$OUT/ws_tls_gen.conf"
./build/aevrix -c "$OUT/ws_tls_gen.conf" > "$OUT"/aevrix_wss.log 2>&1 &
SV=$!
sleep 1.5
for c in handshake echo_text close; do
    run_case "$c" --tls
done
stop_server
trap - EXIT

echo
echo "RESULT: $PASS passed, $FAIL failed"
echo "--- server log tail (ws) ---"
tail -6 "$OUT"/aevrix_ws.log 2>/dev/null
echo "--- server log tail (wss) ---"
tail -6 "$OUT"/aevrix_wss.log 2>/dev/null
[ "$FAIL" -eq 0 ]
