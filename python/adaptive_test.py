#!/usr/bin/env python3
"""
adaptive_test.py — Compare fixed vs adaptive chunking.

Submits the same large query under both modes and compares chunk counts
and timing. Must run against a cluster that has been restarted with the
appropriate `adaptive_chunking` config.

Usage: python3 python/adaptive_test.py <target> <label>
Example: python3 python/adaptive_test.py 127.0.0.1:50051 fixed
"""

import sys
import os
import time
import grpc

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
GENERATED_DIR = os.path.join(SCRIPT_DIR, "generated")
sys.path.insert(0, GENERATED_DIR)

import mini2_pb2
import mini2_pb2_grpc

def run_query(target, label):
    channel = grpc.insecure_channel(
        target,
        options=[
            ("grpc.max_receive_message_length", 16 * 1024 * 1024),
            ("grpc.max_send_message_length", 16 * 1024 * 1024),
        ],
    )
    stub = mini2_pb2_grpc.Mini2ServiceStub(channel)

    client_id = f"adaptive-{label}-{int(time.time())}"

    filter_msg = mini2_pb2.QueryFilter(
        field_name="borough",
        op="eq",
        value="MANHATTAN",
    )
    req = mini2_pb2.QueryRequest(
        client_id=client_id,
        query_text="Find borough eq MANHATTAN",
        filter=filter_msg,
        requested_chunk_records=500,
        max_chunk_bytes=65536,
    )

    t_start = time.time()
    reply = stub.SubmitQuery(req)
    req_id = reply.request_id

    total_records = 0
    chunks = 0
    t_first_chunk = None
    chunk_sizes = []

    while True:
        fetch_req = mini2_pb2.ChunkFetchRequest(
            client_id=client_id,
            request_id=req_id,
            max_records=5000,  # large fetch to not bottleneck on client side
        )
        try:
            chunk = stub.FetchChunk(fetch_req)
        except grpc.RpcError as e:
            print(f"[{label}] FetchChunk failed: {e.details()}")
            break

        count = len(chunk.records)
        total_records += count
        chunks += 1
        chunk_sizes.append(count)

        if t_first_chunk is None and count > 0:
            t_first_chunk = time.time() - t_start

        if chunk.done or chunk.cancelled:
            break

    t_total = time.time() - t_start

    # Compute chunk-size distribution
    non_zero = [s for s in chunk_sizes if s > 0]
    avg_chunk = sum(non_zero) / len(non_zero) if non_zero else 0
    min_chunk = min(non_zero) if non_zero else 0
    max_chunk = max(non_zero) if non_zero else 0

    print(f"\n{'='*60}")
    print(f"  Mode:            {label}")
    print(f"  Total records:   {total_records:,}")
    print(f"  Total chunks:    {chunks:,}")
    print(f"  First chunk:     {t_first_chunk:.3f}s" if t_first_chunk else "  First chunk:     N/A")
    print(f"  Total time:      {t_total:.3f}s")
    print(f"  Avg chunk size:  {avg_chunk:.1f}")
    print(f"  Min chunk size:  {min_chunk}")
    print(f"  Max chunk size:  {max_chunk}")
    print(f"{'='*60}\n")

    return {
        "label": label,
        "records": total_records,
        "chunks": chunks,
        "first_chunk_s": t_first_chunk,
        "total_s": t_total,
        "avg_chunk": avg_chunk,
        "min_chunk": min_chunk,
        "max_chunk": max_chunk,
    }

def main():
    if len(sys.argv) < 3:
        print("usage: python3 adaptive_test.py <target> <label>")
        sys.exit(1)

    target = sys.argv[1]
    label = sys.argv[2]

    print(f"[adaptive_test] running query with label={label}")
    result = run_query(target, label)

    # Write result to a file for later comparison
    out_dir = os.path.join(os.path.dirname(SCRIPT_DIR), "experiments", "adaptive")
    os.makedirs(out_dir, exist_ok=True)
    out_file = os.path.join(out_dir, f"{label}.txt")
    with open(out_file, "w") as f:
        for k, v in result.items():
            f.write(f"{k}={v}\n")
    print(f"[adaptive_test] results written to {out_file}")

if __name__ == "__main__":
    main()
