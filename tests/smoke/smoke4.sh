#!/usr/bin/env bash
set -u
# Resolve repo + scratch locations so the script runs from anywhere.
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
OUT="${SMOKE_OUT:-/tmp/aevrix-smoke}"
mkdir -p "$OUT"
cd "$REPO"

python3 "$HERE/mock_upstream.py" 19006 > "$OUT"/upstream6.log 2>&1 &
UP=$!
sleep 0.3
sed 's/19000/19006/' "$HERE/proxy.conf" > "$OUT"/proxy6.conf
./build/aevrix -c "$OUT"/proxy6.conf > "$OUT"/aevrix7.log 2>&1 &
SV=$!
sleep 0.6

PASS=0
FAIL=0

check() {
    if [ "$1" = "0" ]; then
        PASS=$((PASS + 1)); echo "  PASS: $2"
    else
        FAIL=$((FAIL + 1)); echo "  FAIL: $2"
    fi
}

# req URL [extra curl args...] -> sets STATUS, HEADERS, BODY, RC, TIME
req() {
    local url="$1"; shift
    local raw
    raw=$(curl -sS -i -w '\n#time=%{time_total}' --max-time 6 "$@" "$url" 2>/dev/null)
    RC=$?
    TIME=$(printf '%s' "$raw" | sed -n 's/^#time=//p' | tail -1)
    STATUS=$(printf '%s' "$raw" | sed -n '1p' | tr -d '\r')
    HEADERS=$(printf '%s' "$raw" | sed -n '2,/^$/p' | tr -d '\r')
    BODY=$(printf '%s' "$raw" | awk 'f{print} f==0 && /^\r?$/{f=1}' | sed '/^#time=/d')
}

echo "=== 1. /huge: upstream declares 4MB > 65536-byte limit ==="
req http://127.0.0.1:18080/api/huge
echo "  status=$STATUS time=$TIME rc=$RC"
echo "  body=$BODY"
case "$STATUS" in *502*) check 0 "huge rejected with 502" ;; *) check 1 "huge rejected with 502" ;; esac
case "$BODY" in *"exceeds the configured limit"*) check 0 "huge reports limit message" ;; *) check 1 "huge reports limit message" ;; esac
[ "$RC" = "0" ] && check 0 "huge client did not hang" || check 1 "huge client did not hang"

echo "=== 2. /interim: 103 Early Hints consumed, final response relayed ==="
req http://127.0.0.1:18080/api/interim
echo "  status=$STATUS time=$TIME rc=$RC"
echo "  body=$BODY"
case "$STATUS" in *200*) check 0 "interim final response is 200" ;; *) check 1 "interim final response is 200" ;; esac
case "$BODY" in *"final response"*) check 0 "interim body relayed" ;; *) check 1 "interim body relayed" ;; esac

echo "=== 3. /upgrade: 101 Switching Protocols must be rejected ==="
req http://127.0.0.1:18080/api/upgrade
echo "  status=$STATUS time=$TIME rc=$RC"
echo "  body=$BODY"
case "$STATUS" in *502*) check 0 "101 rejected with 502" ;; *) check 1 "101 rejected with 502" ;; esac
[ "$RC" = "0" ] && check 0 "upgrade client did not hang" || check 1 "upgrade client did not hang"

echo "=== 4. /smuggle: CL + TE framing conflict must be rejected ==="
req http://127.0.0.1:18080/api/smuggle
echo "  status=$STATUS time=$TIME rc=$RC"
echo "  body=$BODY"
case "$STATUS" in *502*) check 0 "smuggling attempt rejected with 502" ;; *) check 1 "smuggling attempt rejected with 502" ;; esac
case "$BODY" in *Content-Length*|*content-length*|*Transfer-Encoding*|*both*) check 0 "conflict explained" ;; *) check 1 "conflict explained" ;; esac

echo "=== 5. /truncate: declared 24 bytes, sent 21, then close ==="
req http://127.0.0.1:18080/api/truncate
echo "  status=$STATUS time=$TIME rc=$RC"
echo "  body=$BODY"
case "$STATUS" in *502*) check 0 "truncated response rejected with 502" ;; *) check 1 "truncated response rejected with 502" ;; esac
case "$BODY" in *truncated*) check 0 "truncation explained" ;; *) check 1 "truncation explained" ;; esac

echo "=== 6. /closedelimited: body until close, re-framed with Content-Length ==="
req http://127.0.0.1:18080/api/closedelimited
echo "  status=$STATUS time=$TIME rc=$RC"
echo "  body=$BODY"
case "$STATUS" in *200*) check 0 "close-delimited response is 200" ;; *) check 1 "close-delimited response is 200" ;; esac
case "$BODY" in *body-until-close*) check 0 "close-delimited body relayed" ;; *) check 1 "close-delimited body relayed" ;; esac
case "$HEADERS" in *"Content-Length: 16"*) check 0 "close-delimited re-framed with Content-Length" ;; *) check 1 "close-delimited re-framed with Content-Length" ;; esac

echo "=== 7. HEAD on /stall: no body expected, must return instantly ==="
req http://127.0.0.1:18080/api/stall -I
echo "  status=$STATUS time=$TIME rc=$RC"
echo "  body=[$BODY]"
case "$STATUS" in *200*) check 0 "HEAD /stall returns 200 at headers" ;; *) check 1 "HEAD /stall returns 200 at headers" ;; esac
[ -z "$BODY" ] && check 0 "HEAD /stall carries no body" || check 1 "HEAD /stall carries no body"
awk "BEGIN{exit !($TIME < 1.5)}" && check 0 "HEAD /stall returned before upstream stall" || check 1 "HEAD /stall returned before upstream stall"

echo "=== 8. HEAD on /huge: representation length kept, no body, instant ==="
req http://127.0.0.1:18080/api/huge -I
echo "  status=$STATUS time=$TIME rc=$RC"
echo "  body=[$BODY]"
case "$STATUS" in *200*) check 0 "HEAD /huge returns 200 at headers" ;; *) check 1 "HEAD /huge returns 200 at headers" ;; esac
case "$HEADERS" in *"Content-Length: 4000000"*) check 0 "HEAD /huge keeps representation length" ;; *) check 1 "HEAD /huge keeps representation length" ;; esac
[ -z "$BODY" ] && check 0 "HEAD /huge carries no body" || check 1 "HEAD /huge carries no body"

echo "=== 9. HEAD on /smuggle: error reply to HEAD must be bodyless ==="
req http://127.0.0.1:18080/api/smuggle -I
echo "  status=$STATUS time=$TIME rc=$RC"
echo "  body=[$BODY]"
case "$STATUS" in *502*) check 0 "HEAD /smuggle returns 502" ;; *) check 1 "HEAD /smuggle returns 502" ;; esac
[ -z "$BODY" ] && check 0 "HEAD error reply carries no body" || check 1 "HEAD error reply carries no body"

echo "=== 10. sequential proxied requests (keep-alive path) ==="
codes=$(curl -sS -o /dev/null -o /dev/null -w '%{http_code}' --max-time 5 \
      http://127.0.0.1:18080/api/hello http://127.0.0.1:18080/api/echo 2>/dev/null)
echo "  codes=$codes"
case "$codes" in 200200) check 0 "sequential proxied requests both succeed" ;; *) check 1 "sequential proxied requests both succeed" ;; esac

kill -TERM "$SV" 2>/dev/null
sleep 0.4
kill -TERM "$UP" 2>/dev/null
wait 2>/dev/null

echo
echo "RESULT: $PASS passed, $FAIL failed"
[ "$FAIL" = "0" ]
