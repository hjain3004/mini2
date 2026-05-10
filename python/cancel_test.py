#!/usr/bin/env python3
"""
cancel_test.py — Submit a large query then cancel it after N chunks.

Usage: python3 python/cancel_test.py <target> <cancel_after_chunks>
Example: python3 python/cancel_test.py 127.0.0.1:50051 3
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

def main():
    if len(sys.argv) < 3:
        print("usage: python3 cancel_test.py <target> <cancel_after_chunks>")
        sys.exit(1)

    target = sys.argv[1]
    cancel_after = int(sys.argv[2])

    print(f"[cancel_test] connecting to {target}")

    channel = grpc.insecure_channel(
        target,
        options=[
            ("grpc.max_receive_message_length", 16 * 1024 * 1024),
            ("grpc.max_send_message_length", 16 * 1024 * 1024),
        ],
    )
    stub = mini2_pb2_grpc.Mini2ServiceStub(channel)

    client_id = f"cancel-test-{int(time.time())}"

    # Large query: all MANHATTAN records.
    filter_msg = mini2_pb2.QueryFilter(
        field_name="borough",
        op="eq",
        value="MANHATTAN",
    )
    req = mini2_pb2.QueryRequest(
        client_id=client_id,
        query_text="Find borough eq MANHATTAN",
        filter=filter_msg,
        requested_chunk_records=1000,
        max_chunk_bytes=65536,
    )

    print(f"[cancel_test] SubmitQuery: {req.query_text}")
    reply = stub.SubmitQuery(req)
    print(f"[cancel_test] QueryAccepted: req_id={reply.request_id}")

    req_id = reply.request_id
    total_records = 0
    chunks = 0

    print(f"[cancel_test] polling FetchChunk, will cancel after {cancel_after} chunks")
    while True:
        fetch_req = mini2_pb2.ChunkFetchRequest(
            client_id=client_id,
            request_id=req_id,
            max_records=1000,
        )
        try:
            chunk = stub.FetchChunk(fetch_req)
        except grpc.RpcError as e:
            print(f"[cancel_test] FetchChunk failed: {e.details()}")
            break

        count = len(chunk.records)
        total_records += count
        chunks += 1
        print(f"  -> chunk {chunk.chunk_id}: {count} records, done={chunk.done}, cancelled={chunk.cancelled}")

        if chunk.done or chunk.cancelled:
            print(f"[cancel_test] query finished naturally (done={chunk.done}, cancelled={chunk.cancelled})")
            break

        if chunks >= cancel_after:
            print(f"\n[cancel_test] >>> Sending CancelQuery for {req_id} <<<")
            cancel_req = mini2_pb2.CancelRequest(
                client_id=client_id,
                request_id=req_id,
                reason="cancel_test: testing step 9",
            )
            try:
                cancel_reply = stub.CancelQuery(cancel_req)
                print(f"[cancel_test] CancelAck: cancelled={cancel_reply.cancelled}")
            except grpc.RpcError as e:
                print(f"[cancel_test] CancelQuery failed: {e.details()}")
                break

            # Fetch one more chunk — should see cancelled=True.
            time.sleep(0.1)
            try:
                final_chunk = stub.FetchChunk(fetch_req)
                print(f"[cancel_test] post-cancel chunk: {len(final_chunk.records)} records, "
                      f"done={final_chunk.done}, cancelled={final_chunk.cancelled}")
            except grpc.RpcError as e:
                print(f"[cancel_test] post-cancel FetchChunk failed: {e.details()}")
            break

    print(f"\n[cancel_test] total fetched: {total_records} records across {chunks} chunks before cancel.")

if __name__ == "__main__":
    main()
