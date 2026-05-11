#!/usr/bin/env bash
# run_linear_vs_indexed_v2.sh — rerun §20.5 with a SELECTIVE predicate
# (complaint_type eq "Noise - Helicopter") on the FULL dataset, so the
# index speedup is actually measurable. A-only to isolate scan cost.

set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

GATEWAY="127.0.0.1:50051"
RUNNER="python3 python/experiment_runner.py"
OUT_DIR="experiments/linear_vs_indexed"
mkdir -p "$OUT_DIR"
CSV="$OUT_DIR/results_v2.csv"

CONF_TMP="config/tree/A_full.conf"
sed 's|dataset_path=data/partitions-small/A.csv|dataset_path=data/partitions/A.csv|' \
    config/tree/A.conf > "$CONF_TMP"

bash scripts/kill_all.sh 2>/dev/null || true
mkdir -p experiments/logs
rm -f experiments/logs/A.log experiments/logs/A.pid

echo "[v2] launching A with FULL dataset..."
./build/mini2_server "$CONF_TMP" > experiments/logs/A.log 2>&1 &
echo $! > experiments/logs/A.pid

# Wait until A logs "loaded" (full dataset = ~3.7M rows = ~60-120s)
echo "[v2] waiting for A to load full shard..."
until grep -q "loaded\|Loaded\|ready\|listening" experiments/logs/A.log 2>/dev/null; do
  sleep 5
done
sleep 5  # ensure server is accepting RPCs

echo "label,records,chunks,submit_ms,first_chunk_ms,total_ms,chunk_records,force_linear" > "$CSV"

PRED_FIELD="complaint_type"
PRED_OP="eq"
PRED_VAL="Noise - Helicopter"

echo "[v2] indexed (5 runs)..."
for RUN in 1 2 3 4 5; do
  $RUNNER $GATEWAY "$PRED_FIELD" "$PRED_OP" "$PRED_VAL" \
          --chunk_records 1000 --max_fetch 5000 \
          --label "indexed_v2_r${RUN}" >> "$CSV"
  sleep 1
done

echo "[v2] linear (5 runs)..."
for RUN in 1 2 3 4 5; do
  $RUNNER $GATEWAY "$PRED_FIELD" "$PRED_OP" "$PRED_VAL" \
          --chunk_records 1000 --max_fetch 5000 --force_linear \
          --label "linear_v2_r${RUN}" >> "$CSV"
  sleep 1
done

bash scripts/kill_all.sh 2>/dev/null || true
rm -f "$CONF_TMP"

echo "[v2] done. results: $CSV"
echo ""
echo "--- summary ---"
awk -F',' 'NR>1 {
  k = (substr($1,1,7) == "indexed") ? "indexed" : "linear";
  rec[k]=$2; n[k]++; ttot[k]+=$6; tfirst[k]+=$5;
}
END {
  for (k in n) {
    printf "%-8s  records=%s  mean_first_ms=%.1f  mean_total_ms=%.1f  (n=%d)\n",
      k, rec[k], tfirst[k]/n[k], ttot[k]/n[k], n[k]
  }
}' "$CSV"
