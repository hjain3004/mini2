#!/usr/bin/env bash
# Compares scheduler policies (greedy vs round_robin) under concurrent client load.
#
# What it does:
#   1. For each policy in {greedy, round_robin}:
#      a. sed-flips scheduler_policy in every config/tree/*.conf
#      b. Restarts the 9-node cluster
#      c. Waits for all nodes (incl. Python I) ready
#      d. Runs python/fairness_test.py: 3 concurrent clients (huge / medium / small)
#      e. Records timings
#   2. Prints both runs side-by-side at the end.
#   3. Restores configs to round_robin (default).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

GATEWAY=${GATEWAY:-127.0.0.1:50051}
CHUNK_RECORDS=${CHUNK_RECORDS:-1000}

set_policy() {
  local p=$1
  for f in config/tree/*.conf; do
    if grep -q "^scheduler_policy=" "$f"; then
      sed -i '' "s/^scheduler_policy=.*/scheduler_policy=$p/" "$f"
    else
      echo "scheduler_policy=$p" >> "$f"
    fi
  done
}

wait_ready() {
  echo "[test] waiting for all 9 nodes to be ready..."
  # Heartbeats from B,C,D,E,F,G,H to A imply C++ side ready.
  until grep -q "received heartbeat from B" experiments/logs/A.log 2>/dev/null \
     && grep -q "received heartbeat from H" experiments/logs/A.log 2>/dev/null; do
    sleep 1
  done
  # Python I takes longer due to dataset load.
  until grep -q "I running" experiments/logs/I.log 2>/dev/null; do
    sleep 1
  done
  # Plus a small grace for I to finish heartbeating with A.
  until grep -q "received heartbeat from I" experiments/logs/A.log 2>/dev/null; do
    sleep 1
  done
  echo "[test] all 9 ready."
}

run_for_policy() {
  local p=$1
  local out=$2
  echo
  echo "==================== POLICY: $p ===================="
  set_policy "$p"
  bash scripts/kill_all.sh
  bash scripts/launch_all.sh
  wait_ready
  python3 python/fairness_test.py "$GATEWAY" "$CHUNK_RECORDS" | tee "$out"
}

mkdir -p experiments/fairness
RR_OUT="experiments/fairness/round_robin.txt"
GR_OUT="experiments/fairness/greedy.txt"

run_for_policy round_robin "$RR_OUT"
run_for_policy greedy      "$GR_OUT"

echo
echo "==================== SUMMARY ===================="
echo "--- round_robin ---"
cat "$RR_OUT"
echo
echo "--- greedy ---"
cat "$GR_OUT"

# Restore default
set_policy round_robin
bash scripts/kill_all.sh
echo
echo "[test] configs restored to round_robin; cluster stopped."
