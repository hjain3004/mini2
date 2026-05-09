#!/usr/bin/env python3
"""
partition_data.py — Partition the NYC 311 CSV into 9 shards.

Reads dataset/311_2020.csv line-by-line (streaming, no full in-memory load),
hashes unique_key % 9, and writes to data/partitions/{A..I}.csv.

Usage: python3 scripts/partition_data.py [input_csv] [output_dir]
  Defaults: dataset/311_2020.csv → data/partitions/
"""

import csv
import hashlib
import os
import sys

NODE_NAMES = list("ABCDEFGHI")
NUM_NODES = len(NODE_NAMES)


def shard_for_key(unique_key: str) -> int:
    """Deterministic shard assignment: hash(unique_key) % 9."""
    try:
        return int(unique_key) % NUM_NODES
    except (ValueError, TypeError):
        # Fallback for non-numeric keys
        return int(hashlib.md5(str(unique_key).encode()).hexdigest(), 16) % NUM_NODES


def main():
    input_csv = sys.argv[1] if len(sys.argv) > 1 else "dataset/311_2020.csv"
    output_dir = sys.argv[2] if len(sys.argv) > 2 else "data/partitions"

    os.makedirs(output_dir, exist_ok=True)

    print(f"[partition] reading {input_csv}")
    print(f"[partition] output dir: {output_dir}")

    # Open input and all output files
    with open(input_csv, "r", newline="", encoding="utf-8", errors="replace") as fin:
        reader = csv.reader(fin)
        header = next(reader)

        # Open output files and writers
        out_files = {}
        writers = {}
        for i, name in enumerate(NODE_NAMES):
            path = os.path.join(output_dir, f"{name}.csv")
            f = open(path, "w", newline="", encoding="utf-8")
            out_files[i] = f
            w = csv.writer(f)
            w.writerow(header)
            writers[i] = w

        counts = [0] * NUM_NODES
        total = 0
        errors = 0

        for row in reader:
            total += 1
            if total % 1_000_000 == 0:
                print(f"[partition] {total:,} rows processed...")

            if not row:
                continue

            try:
                shard = shard_for_key(row[0])  # column 0 = Unique Key
                writers[shard].writerow(row)
                counts[shard] += 1
            except Exception:
                errors += 1

        # Close all output files
        for f in out_files.values():
            f.close()

    print(f"\n[partition] done. {total:,} rows total, {errors} errors.")
    print("[partition] per-shard counts:")
    for i, name in enumerate(NODE_NAMES):
        print(f"  {name}: {counts[i]:,} records")


if __name__ == "__main__":
    main()
