#!/usr/bin/env bash
set -u
# Resolve repo + scratch locations so the script runs from anywhere.
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
OUT="${SMOKE_OUT:-/tmp/aevrix-smoke}"
mkdir -p "$OUT"
cd "$REPO"
python3 "$HERE/mock_upstream.py" 19002 > "$OUT"/upstream3.log 2>&1 &
UP=$!
sleep 0.3
sed 's/19000/19002/' "$HERE/proxy.conf" > "$OUT"/proxy3.conf
./build/aevrix -c "$OUT"/proxy3.conf > "$OUT"/aevrix3.log 2>&1 &
SV=$!
sleep 0.6
echo "=== A. HEAD on a STATIC path (proxy not involved) ==="
curl -sS -I --max-time 4 http://127.0.0.1:18080/index.html; echo "rc=$?"
echo "=== B. HEAD through the proxy ==="
curl -sS -I --max-time 4 http://127.0.0.1:18080/api/hello; echo "rc=$?"
echo "=== C. GET through the proxy right after ==="
curl -sS -o /dev/null -w 'get=%{http_code}\n' --max-time 4 http://127.0.0.1:18080/api/hello; echo "rc=$?"
kill -TERM $SV 2>/dev/null; sleep 0.4; kill -TERM $UP 2>/dev/null; wait 2>/dev/null
echo "=== upstream log ==="; cat "$OUT"/upstream3.log
echo "=== aevrix log tail ==="; grep -iE "proxy|timeout|error|warn" "$OUT"/aevrix3.log | tail -6
