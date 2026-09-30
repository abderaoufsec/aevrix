#!/usr/bin/env bash
set -u
# Resolve repo + scratch locations so the script runs from anywhere.
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
OUT="${SMOKE_OUT:-/tmp/aevrix-smoke}"
mkdir -p "$OUT"
cd "$REPO"
python3 "$HERE/mock_upstream.py" 19001 > "$OUT"/upstream2.log 2>&1 &
UP=$!
sleep 0.3
sed 's/19000/19001/' "$HERE/proxy.conf" > "$OUT"/proxy2.conf
./build/aevrix -c "$OUT"/proxy2.conf > "$OUT"/aevrix2.log 2>&1 &
SV=$!
sleep 0.6
echo "=== stalled upstream (expect 504 ~3s) ==="
time curl -sS -i --max-time 8 http://127.0.0.1:18080/api/stall
echo
echo "=== HEAD (expect 200 + CL, no body) ==="
time curl -sS -i -X HEAD --max-time 6 http://127.0.0.1:18080/api/hello
echo
echo "=== proxy sockets after both ==="
ss -tnp 2>/dev/null | grep -E "19001|18080" | head -10
kill -TERM $SV 2>/dev/null; sleep 0.5; kill -TERM $UP 2>/dev/null; wait 2>/dev/null
echo "=== aevrix log (proxy lines) ==="
grep -iE "proxy|timeout|504|warn|err" "$OUT"/aevrix2.log | tail -12
