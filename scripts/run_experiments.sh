#!/usr/bin/env bash
# run_experiments.sh — Run all four experiments for the report.
#
# §20.1  Chunk size sweep (100, 500, 1000, 2000, 5000)
# §20.2  Fairness (round_robin vs greedy) — uses fairness_test.py
# §20.4  Local-vs-distributed (A alone vs full tree)
# §20.5  Linear-vs-indexed scan
#
# Outputs CSV files to experiments/{chunk_size,fairness,local_vs_distributed,linear_vs_indexed}/

set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

GATEWAY="127.0.0.1:50051"
RUNNER="python3 python/experiment_runner.py"

ensure_configs_default() {
  for conf in config/tree/*.conf; do
    sed -i '' 's/adaptive_chunking=true/adaptive_chunking=false/' "$conf" 2>/dev/null || true
    sed -i '' 's/scheduler_policy=greedy/scheduler_policy=round_robin/' "$conf" 2>/dev/null || true
  done
}

restart_cluster() {
  bash scripts/kill_all.sh 2>/dev/null || true
  rm -f experiments/logs/*.log experiments/logs/*.pid
  bash scripts/launch_all.sh
  echo "  waiting 30s for dataset load..."
  sleep 30
}

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
echo "╔═══════════════════════════════════════════════════════════╗"
echo "║          Step 11: Running All Four Experiments           ║"
echo "╚═══════════════════════════════════════════════════════════╝"

# ─── §20.1 Chunk Size Sweep ──────────────────────────────────────────────────
echo ""
echo "━━━ Experiment 1/4: Chunk Size Sweep ━━━"
ensure_configs_default
restart_cluster

OUT_DIR="experiments/chunk_size"
mkdir -p "$OUT_DIR"
CSV="$OUT_DIR/results.csv"
echo "label,records,chunks,submit_ms,first_chunk_ms,total_ms,chunk_records,force_linear" > "$CSV"

for SIZE in 100 500 1000 2000 5000; do
  echo "  chunk_records=$SIZE ..."
  for RUN in 1 2 3; do
    $RUNNER $GATEWAY borough eq MANHATTAN --chunk_records "$SIZE" \
            --max_fetch "$SIZE" --label "chunk_${SIZE}_r${RUN}" >> "$CSV"
    sleep 1
  done
done
echo "  results: $CSV"

# ─── §20.2 Fairness ─────────────────────────────────────────────────────────
echo ""
echo "━━━ Experiment 2/4: Fairness (round_robin vs greedy) ━━━"
OUT_DIR="experiments/fairness"
mkdir -p "$OUT_DIR"

# round_robin (already the default)
ensure_configs_default
restart_cluster
echo "  running round_robin..."
python3 python/fairness_test.py $GATEWAY 1000 | tee "$OUT_DIR/round_robin.txt"
sleep 2

# greedy
for conf in config/tree/*.conf; do
  sed -i '' 's/scheduler_policy=round_robin/scheduler_policy=greedy/' "$conf"
done
restart_cluster
echo "  running greedy..."
python3 python/fairness_test.py $GATEWAY 1000 | tee "$OUT_DIR/greedy.txt"

# restore
ensure_configs_default

# ─── §20.4 Local-vs-Distributed ─────────────────────────────────────────────
echo ""
echo "━━━ Experiment 3/4: Local vs Distributed ━━━"
OUT_DIR="experiments/local_vs_distributed"
mkdir -p "$OUT_DIR"
CSV="$OUT_DIR/results.csv"
echo "label,records,chunks,submit_ms,first_chunk_ms,total_ms,chunk_records,force_linear" > "$CSV"

# Full distributed (9 nodes)
ensure_configs_default
restart_cluster
echo "  distributed (9 nodes)..."
for RUN in 1 2 3; do
  $RUNNER $GATEWAY borough eq MANHATTAN --chunk_records 1000 \
          --max_fetch 5000 --label "distributed_r${RUN}" >> "$CSV"
  sleep 1
done

# Local only (A alone, no other nodes)
bash scripts/kill_all.sh 2>/dev/null || true
rm -f experiments/logs/*.log experiments/logs/*.pid
# Start only A
./build/mini2_server config/tree/A.conf > experiments/logs/A.log 2>&1 &
echo $! > experiments/logs/A.pid
echo "  waiting 25s for A to load dataset..."
sleep 25
echo "  local (A only)..."
for RUN in 1 2 3; do
  $RUNNER $GATEWAY borough eq MANHATTAN --chunk_records 1000 \
          --max_fetch 5000 --label "local_r${RUN}" >> "$CSV"
  sleep 1
done
echo "  results: $CSV"

# ─── §20.5 Linear-vs-Indexed ────────────────────────────────────────────────
echo ""
echo "━━━ Experiment 4/4: Linear vs Indexed Scan ━━━"
OUT_DIR="experiments/linear_vs_indexed"
mkdir -p "$OUT_DIR"
CSV="$OUT_DIR/results.csv"
echo "label,records,chunks,submit_ms,first_chunk_ms,total_ms,chunk_records,force_linear" > "$CSV"

# Use A-only for cleaner measurement (no network noise)
# A should already be running from the previous experiment
echo "  indexed scan..."
for RUN in 1 2 3; do
  $RUNNER $GATEWAY borough eq MANHATTAN --chunk_records 1000 \
          --max_fetch 5000 --label "indexed_r${RUN}" >> "$CSV"
  sleep 1
done

echo "  linear scan..."
for RUN in 1 2 3; do
  $RUNNER $GATEWAY borough eq MANHATTAN --chunk_records 1000 \
          --max_fetch 5000 --force_linear --label "linear_r${RUN}" >> "$CSV"
  sleep 1
done
echo "  results: $CSV"

# ─── Cleanup ─────────────────────────────────────────────────────────────────
bash scripts/kill_all.sh 2>/dev/null || true
ensure_configs_default

echo ""
echo "╔═══════════════════════════════════════════════════════════╗"
echo "║          All experiments complete!                       ║"
echo "║          Run: python3 python/plot_experiments.py         ║"
echo "╚═══════════════════════════════════════════════════════════╝"
