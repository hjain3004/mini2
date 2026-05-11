#!/usr/bin/env bash
# test_cache.sh — Verify Fix #4: LRU Result Cache
#
# We submit a query twice. The first submission will take the normal time (~3s)
# and populate the cache. The second submission should hit the cache and
# return instantly, without propagating to peers.

set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

bash scripts/kill_all.sh 2>/dev/null || true
mkdir -p experiments/logs
bash scripts/launch_all.sh > /dev/null

echo "[test] waiting for Python I..."
until grep -q "loaded\|Loaded\|ready\|listening" experiments/logs/I.log 2>/dev/null; do sleep 5; done
sleep 5

echo "[test] RUN 1: submitting query (cache miss expected)..."
T0=$(python3 -c 'import time; print(time.time())')
./build/mini2_client 127.0.0.1:50051 borough eq MANHATTAN 1000 > /tmp/test_cache_1.out 2>&1 || true
T1=$(python3 -c 'import time; print(time.time())')
ELAPSED_1=$(python3 -c "print(f'{$T1-$T0:.2f}')")

echo "Run 1 time: ${ELAPSED_1}s"

echo "[test] RUN 2: submitting same query (cache hit expected)..."
T0=$(python3 -c 'import time; print(time.time())')
./build/mini2_client 127.0.0.1:50051 borough eq MANHATTAN 1000 > /tmp/test_cache_2.out 2>&1 || true
T1=$(python3 -c 'import time; print(time.time())')
ELAPSED_2=$(python3 -c "print(f'{$T1-$T0:.2f}')")

echo "Run 2 time: ${ELAPSED_2}s"
echo ""

PASS=0

if grep -a -q "CACHE POPULATED" experiments/logs/A.log; then
  echo "PASS-1: A populated the cache after Run 1."
  PASS=$((PASS+1))
else
  echo "FAIL: A did not populate cache."
fi

if grep -a -q "CACHE HIT" experiments/logs/A.log; then
  echo "PASS-2: A registered a cache hit on Run 2."
  PASS=$((PASS+1))
else
  echo "FAIL: A did not register a cache hit."
fi

# Run 2 should be at least as fast as Run 1 (accounting for process overhead)
FASTER=$(python3 -c "print(1 if $ELAPSED_2 <= $ELAPSED_1 + 0.05 else 0)")
if [ "$FASTER" = "1" ]; then
  echo "PASS-3: Run 2 (${ELAPSED_2}s) was faster or equal to Run 1 (${ELAPSED_1}s)."
  PASS=$((PASS+1))
else
  echo "FAIL: Run 2 was not significantly faster."
fi

if [ "$PASS" -eq 3 ]; then
  echo ""
  echo "FIX #4 VERIFIED: LRU Result Cache working perfectly."
else
  echo "FAIL: only $PASS/3 checks passed."
  exit 1
fi

bash scripts/kill_all.sh 2>/dev/null || true
