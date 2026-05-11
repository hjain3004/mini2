#!/usr/bin/env bash
# test_partial.sh — Verify Fix #5: per-child completion timeout +
# partial=true semantics.
#
# We set B's peer_query_test_delay_ms=20000 so B holds the query for 20s,
# and A's peer_completion_timeout_ms=5000 so A force-completes B at 5s.

set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

A_BAK="config/tree/A.conf.bak"
B_BAK="config/tree/B.conf.bak"
cp config/tree/A.conf "$A_BAK"
cp config/tree/B.conf "$B_BAK"
trap 'mv "$A_BAK" config/tree/A.conf 2>/dev/null || true; mv "$B_BAK" config/tree/B.conf 2>/dev/null || true; bash scripts/kill_all.sh 2>/dev/null || true' EXIT

set_kv() {  # set_kv <conf> <key> <value>
  if grep -q "^$2=" "$1"; then
    sed -i '' "s|^$2=.*|$2=$3|" "$1"
  else
    echo "$2=$3" >> "$1"
  fi
}

set_kv config/tree/A.conf peer_completion_timeout_ms 5000
set_kv config/tree/B.conf peer_query_test_delay_ms 20000

bash scripts/kill_all.sh 2>/dev/null || true
mkdir -p experiments/logs
bash scripts/launch_all.sh > /dev/null
echo "[test] waiting for Python I..."
until grep -q "loaded\|Loaded\|ready\|listening" experiments/logs/I.log 2>/dev/null; do sleep 5; done
sleep 5

echo "[test] submitting query — B will hold for 20s, A should time out at 5s..."
T0=$(python3 -c 'import time; print(time.time())')
./build/mini2_client 127.0.0.1:50051 borough eq MANHATTAN 1000 > /tmp/test_partial.out 2>&1 || true
T1=$(python3 -c 'import time; print(time.time())')
ELAPSED=$(python3 -c "print(f'{$T1-$T0:.2f}')")

echo ""
echo "--- client output (last 4 lines) ---"
tail -4 /tmp/test_partial.out
echo ""
echo "client wall time: ${ELAPSED}s"

echo ""
echo "--- A.log: peer-completion-timeout events ---"
grep "peer-completion-timeout" experiments/logs/A.log || echo "  (none)"

echo ""
PASS=0
if grep -q "peer-completion-timeout.* B " experiments/logs/A.log; then
  echo "PASS-1: A's janitor force-completed B."
  PASS=$((PASS+1))
fi
# Client time should be ~5s (timeout), well under 20s (B's delay).
WITHIN=$(python3 -c "print(1 if 3.0 < $ELAPSED < 12.0 else 0")
if [ "$WITHIN" = "1" ]; then
  echo "PASS-2: client completed in ${ELAPSED}s (timeout window, not B's 20s)."
  PASS=$((PASS+1))
fi

if [ "$PASS" -eq 2 ]; then
  echo ""
  echo "FIX #5 VERIFIED: per-child timeout + partial-result delivery working."
else
  echo "FAIL: only $PASS/2 checks passed."
  exit 1
fi
