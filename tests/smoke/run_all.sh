#!/usr/bin/env bash
# Run every smoke script in sequence (Phase 22 proxy + Phase 23 WebSocket).
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

FAILED=0
for s in smoke1.sh smoke2.sh smoke3.sh smoke4.sh smoke_ws.sh smoke_reload.sh; do
    echo "################ $s ################"
    if bash "$HERE/$s"; then
        echo "=> $s exited 0"
    else
        echo "=> $s FAILED"
        FAILED=$((FAILED + 1))
    fi
    echo
done

if [ "$FAILED" -eq 0 ]; then
    echo "ALL SMOKE SCRIPTS PASSED"
else
    echo "$FAILED SMOKE SCRIPT(S) FAILED"
fi
exit "$FAILED"
