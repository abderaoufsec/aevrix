#!/usr/bin/env bash
set -u
# Resolve repo + scratch locations so the script runs from anywhere.
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
OUT="${SMOKE_OUT:-/tmp/aevrix-smoke}"
mkdir -p "$OUT"
cd "$REPO"
python3 "$HERE/mock_upstream.py" 19000 > "$OUT"/upstream.log 2>&1 &
UP=$!
sleep 0.4
./build/aevrix -c "$HERE/proxy.conf" > "$OUT"/aevrix.log 2>&1 &
SV=$!
sleep 0.8

echo "=== 1. plain GET, prefix stripped ==="
curl -sS -i --max-time 5 http://127.0.0.1:18080/api/hello
echo
echo "=== 2. /echo shows rewritten target + X-Forwarded-For ==="
curl -sS --max-time 5 -H 'X-Forwarded-For: 203.0.113.7' http://127.0.0.1:18080/api/echo
echo
echo "=== 3. chunked upstream re-framed for client ==="
curl -sS -i --max-time 5 http://127.0.0.1:18080/api/chunked
echo
echo "=== 4. upstream status passthrough ==="
curl -sS -i --max-time 5 http://127.0.0.1:18080/api/status/404
echo
echo "=== 5. HEAD request ==="
curl -sS -I --max-time 5 http://127.0.0.1:18080/api/hello
echo "=== 6. non-proxy path still served from disk ==="
curl -sS -o /dev/null -w 'static=%{http_code} bytes=%{size_download}\n' --max-time 5 http://127.0.0.1:18080/index.html
echo "=== 7. five keep-alive requests on one client connection ==="
curl -sS --max-time 8 -o /dev/null -w '%{http_code} ' http://127.0.0.1:18080/api/a http://127.0.0.1:18080/api/b http://127.0.0.1:18080/api/c http://127.0.0.1:18080/api/d http://127.0.0.1:18080/api/e
echo
kill -TERM $SV 2>/dev/null; sleep 0.4; kill -TERM $UP 2>/dev/null; wait 2>/dev/null
echo "=== server log tail ==="
tail -14 "$OUT"/aevrix.log
echo "=== upstream log ==="
cat "$OUT"/upstream.log
