#!/usr/bin/env bash
# ──────────────────────────────────────────────────────────────────────────────
# make_small_partitions.sh — Create small test partitions from the full ones.
#
# Usage:  bash scripts/make_small_partitions.sh [LINES_PER_SHARD]
#
# Default: 5000 lines per shard (~2-3 MB each, ~25 MB total).
# The full partitions (~1.4 GB each) remain untouched in data/partitions/.
# Small ones go to data/partitions-small/ and configs are updated to point there.
# ──────────────────────────────────────────────────────────────────────────────
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LINES="${1:-5000}"
SRC="$ROOT/data/partitions"
DST="$ROOT/data/partitions-small"

echo "Creating small partitions (${LINES} lines each)..."

mkdir -p "$DST"

for node in A B C D E F G H I; do
  src_file="$SRC/${node}.csv"
  dst_file="$DST/${node}.csv"

  if [ ! -f "$src_file" ]; then
    echo "  SKIP: $src_file not found"
    continue
  fi

  # Copy header + first N data lines
  head -1 "$src_file" > "$dst_file"
  tail -n +2 "$src_file" | head -n "$LINES" >> "$dst_file"

  src_size=$(du -h "$src_file" | cut -f1)
  dst_size=$(du -h "$dst_file" | cut -f1)
  lines=$(wc -l < "$dst_file" | tr -d ' ')
  echo "  ✔ ${node}.csv: ${src_size} → ${dst_size} (${lines} lines)"
done

echo ""
echo "Done! Small partitions in: data/partitions-small/"
echo ""
echo "To use them, update your config files:"
echo "  # Switch to small partitions:"
echo "  for f in config/tree/*.conf; do"
echo "    sed -i '' 's|data/partitions/|data/partitions-small/|' \"\$f\""
echo "  done"
echo ""
echo "  # Switch back to full partitions:"
echo "  for f in config/tree/*.conf; do"
echo "    sed -i '' 's|data/partitions-small/|data/partitions/|' \"\$f\""
echo "  done"
