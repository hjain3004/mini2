#!/usr/bin/env bash
# test_adaptive_chunking.sh — Compare fixed vs adaptive chunking.
#
# Restarts the cluster under each mode and runs the adaptive_test.py probe.
# Results written to experiments/adaptive/{fixed,adaptive}.txt.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

echo "=== Step 10: Adaptive Chunking Comparison ==="

# ─── Fixed (baseline) ──────────────────────────────────────────────────────

echo ""
echo "--- Phase 1: FIXED chunking (adaptive_chunking=false) ---"

# Ensure all configs have adaptive_chunking=false
for conf in config/tree/*.conf; do
  if grep -q "adaptive_chunking" "$conf"; then
    sed -i '' 's/adaptive_chunking=true/adaptive_chunking=false/' "$conf"
  fi
done

bash scripts/kill_all.sh 2>/dev/null || true
rm -f experiments/logs/*.log experiments/logs/*.pid
bash scripts/launch_all.sh
echo "  waiting 30s for dataset load..."
sleep 30

echo "  running fixed-mode query..."
python3 python/adaptive_test.py 127.0.0.1:50051 fixed

# ─── Adaptive ───────────────────────────────────────────────────────────────

echo ""
echo "--- Phase 2: ADAPTIVE chunking (adaptive_chunking=true) ---"

# Flip all configs to adaptive_chunking=true
for conf in config/tree/*.conf; do
  sed -i '' 's/adaptive_chunking=false/adaptive_chunking=true/' "$conf"
done

bash scripts/kill_all.sh 2>/dev/null || true
rm -f experiments/logs/*.log experiments/logs/*.pid
bash scripts/launch_all.sh
echo "  waiting 30s for dataset load..."
sleep 30

echo "  running adaptive-mode query..."
python3 python/adaptive_test.py 127.0.0.1:50051 adaptive

# ─── Restore + Summary ─────────────────────────────────────────────────────

# Restore to fixed (default)
for conf in config/tree/*.conf; do
  sed -i '' 's/adaptive_chunking=true/adaptive_chunking=false/' "$conf"
done

bash scripts/kill_all.sh 2>/dev/null || true

echo ""
echo "=== Results ==="
echo ""
echo "--- FIXED ---"
cat experiments/adaptive/fixed.txt 2>/dev/null || echo "(not found)"
echo ""
echo "--- ADAPTIVE ---"
cat experiments/adaptive/adaptive.txt 2>/dev/null || echo "(not found)"
echo ""
echo "Done."
