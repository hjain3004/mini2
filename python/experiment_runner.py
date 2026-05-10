#!/usr/bin/env python3
"""
experiment_runner.py — Parametric query runner that outputs CSV rows.

Runs a single query against the gateway and reports timing/chunk metrics
as a CSV-compatible line. Used by run_experiments.sh.

Usage:
  python3 python/experiment_runner.py <target> <field> <op> <value> \
      [--chunk_records N] [--force_linear] [--label LABEL] [--max_fetch N]
"""

import os, sys, time, argparse, grpc

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(SCRIPT_DIR, "generated"))

import mini2_pb2, mini2_pb2_grpc


def run_query(args):
    channel = grpc.insecure_channel(
        args.target,
        options=[
            ("grpc.max_receive_message_length", 16 * 1024 * 1024),
            ("grpc.max_send_message_length", 16 * 1024 * 1024),
        ],
    )
    stub = mini2_pb2_grpc.Mini2ServiceStub(channel)

    client_id = f"exp-{args.label}-{int(time.time()*1000)}"
    filter_msg = mini2_pb2.QueryFilter(
        field_name=args.field, op=args.op, value=args.value,
    )
    req = mini2_pb2.QueryRequest(
        client_id=client_id,
        query_text=f"{args.field} {args.op} {args.value}",
        filter=filter_msg,
        requested_chunk_records=args.chunk_records,
        max_chunk_bytes=65536,
        force_linear_scan=args.force_linear,
    )

    t_start = time.time()
    reply = stub.SubmitQuery(req)
    if not reply.accepted:
        print(f"ERROR: {reply.message}", file=sys.stderr)
        sys.exit(1)
    req_id = reply.request_id
    t_submit = time.time() - t_start

    total_records = 0
    chunks = 0
    t_first_chunk = None
    max_fetch = args.max_fetch if args.max_fetch > 0 else args.chunk_records

    while True:
        fetch_req = mini2_pb2.ChunkFetchRequest(
            client_id=client_id, request_id=req_id, max_records=max_fetch,
        )
        try:
            chunk = stub.FetchChunk(fetch_req)
        except grpc.RpcError as e:
            print(f"ERROR: {e.details()}", file=sys.stderr)
            break

        n = len(chunk.records)
        total_records += n
        chunks += 1

        if t_first_chunk is None and n > 0:
            t_first_chunk = time.time() - t_start

        if chunk.done or chunk.cancelled:
            break

    t_total = time.time() - t_start

    # Output: label,records,chunks,submit_ms,first_chunk_ms,total_ms,chunk_records,force_linear
    first_ms = f"{t_first_chunk*1000:.1f}" if t_first_chunk else "0"
    print(f"{args.label},{total_records},{chunks},"
          f"{t_submit*1000:.1f},{first_ms},{t_total*1000:.1f},"
          f"{args.chunk_records},{1 if args.force_linear else 0}")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("target")
    p.add_argument("field")
    p.add_argument("op")
    p.add_argument("value")
    p.add_argument("--chunk_records", type=int, default=500)
    p.add_argument("--force_linear", action="store_true")
    p.add_argument("--label", default="default")
    p.add_argument("--max_fetch", type=int, default=0)
    args = p.parse_args()
    run_query(args)


if __name__ == "__main__":
    main()
