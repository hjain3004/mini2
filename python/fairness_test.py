#!/usr/bin/env python3
"""
fairness_test.py — Run multiple concurrent clients against gateway A and
report per-client completion timing. Used to compare scheduler policies
(greedy vs round_robin).

Usage:
    python3 python/fairness_test.py <gateway> [<chunk_records>]
Example:
    python3 python/fairness_test.py 127.0.0.1:50051 1000
"""

import os
import sys
import time
import threading

import grpc

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
GENERATED_DIR = os.path.join(SCRIPT_DIR, "generated")
sys.path.insert(0, GENERATED_DIR)

import mini2_pb2
import mini2_pb2_grpc


CLIENTS = [
    # (label, field, op, value)
    ("HUGE-manhattan",    "borough",        "eq", "MANHATTAN"),
    ("MED-staten-island", "borough",        "eq", "STATEN_ISLAND"),
    ("SMALL-noise-veh",   "complaint_type", "eq", "Noise - Vehicle"),
]


def run_client(label, field, op, value, gateway, chunk_records, results):
    channel = grpc.insecure_channel(
        gateway,
        options=[
            ("grpc.max_receive_message_length", 16 * 1024 * 1024),
            ("grpc.max_send_message_length", 16 * 1024 * 1024),
        ],
    )
    stub = mini2_pb2_grpc.Mini2ServiceStub(channel)

    filt = mini2_pb2.QueryFilter(field_name=field, op=op, value=value)
    submit_req = mini2_pb2.QueryRequest(
        client_id=f"{label}-{int(time.time())}",
        query_text=f"{field} {op} {value}",
        filter=filt,
        requested_chunk_records=chunk_records,
        max_chunk_bytes=65536,
    )

    t_submit = time.time()
    accepted = stub.SubmitQuery(submit_req)
    if not accepted.accepted:
        results[label] = {"error": accepted.message}
        return

    request_id = accepted.request_id
    total = 0
    chunks = 0
    t_first_chunk = None

    while True:
        fetch_req = mini2_pb2.ChunkFetchRequest(
            client_id=submit_req.client_id,
            request_id=request_id,
            max_records=chunk_records,
        )
        chunk = stub.FetchChunk(fetch_req)
        n = len(chunk.records)
        if n > 0 and t_first_chunk is None:
            t_first_chunk = time.time()
        total += n
        chunks += 1
        if chunk.done:
            break

    t_done = time.time()
    results[label] = {
        "records": total,
        "chunks": chunks,
        "time_to_first_chunk_s": (t_first_chunk - t_submit) if t_first_chunk else None,
        "total_s": t_done - t_submit,
    }


def main():
    if len(sys.argv) < 2:
        print("usage: fairness_test.py <gateway> [<chunk_records>]")
        sys.exit(1)

    gateway = sys.argv[1]
    chunk_records = int(sys.argv[2]) if len(sys.argv) > 2 else 1000

    results = {}
    threads = []

    print(f"[fairness] launching {len(CLIENTS)} concurrent clients against {gateway} "
          f"(chunk_records={chunk_records})", flush=True)
    t0 = time.time()
    for label, field, op, value in CLIENTS:
        t = threading.Thread(
            target=run_client,
            args=(label, field, op, value, gateway, chunk_records, results),
            daemon=False,
        )
        t.start()
        threads.append(t)

    for t in threads:
        t.join()
    t_total = time.time() - t0

    print(f"\n[fairness] all clients done in {t_total:.3f}s")
    print(f"{'label':<20} {'records':>10} {'chunks':>8} "
          f"{'first_chunk_s':>14} {'total_s':>10}")
    print("-" * 66)
    # Sort by total_s ascending so the "winner" appears first
    for label, r in sorted(results.items(),
                           key=lambda kv: kv[1].get("total_s", 9e9)):
        if "error" in r:
            print(f"{label:<20} ERROR: {r['error']}")
            continue
        first = (f"{r['time_to_first_chunk_s']:.3f}"
                 if r['time_to_first_chunk_s'] is not None else "-")
        print(f"{label:<20} {r['records']:>10} {r['chunks']:>8} "
              f"{first:>14} {r['total_s']:>10.3f}")


if __name__ == "__main__":
    main()
