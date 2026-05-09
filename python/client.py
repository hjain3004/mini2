#!/usr/bin/env python3
"""
client.py — External client to test A-only query flow.

Usage: python3 python/client.py <target_node> <field> <op> <value> [max_records]
Example: python3 python/client.py 127.0.0.1:50051 borough eq MANHATTAN 500
"""

import sys
import os
import time
import grpc

# Add generated dir to path
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
GENERATED_DIR = os.path.join(SCRIPT_DIR, "generated")
sys.path.insert(0, GENERATED_DIR)

import mini2_pb2
import mini2_pb2_grpc

def main():
    if len(sys.argv) < 5:
        print("usage: python3 client.py <target> <field> <op> <value> [max_chunk_records]")
        sys.exit(1)

    target = sys.argv[1]
    field = sys.argv[2]
    op = sys.argv[3]
    value = sys.argv[4]
    max_records = int(sys.argv[5]) if len(sys.argv) > 5 else 500

    print(f"[client] connecting to {target}")
    
    channel = grpc.insecure_channel(
        target,
        options=[
            ("grpc.max_receive_message_length", 16 * 1024 * 1024),
            ("grpc.max_send_message_length", 16 * 1024 * 1024),
        ],
    )
    stub = mini2_pb2_grpc.Mini2ServiceStub(channel)

    client_id = f"client-{int(time.time())}"

    # Build filter
    filter_msg = mini2_pb2.QueryFilter(
        field_name=field,
        op=op,
        value=value,
    )

    req = mini2_pb2.QueryRequest(
        client_id=client_id,
        query_text=f"Find {field} {op} {value}",
        filter=filter_msg,
        requested_chunk_records=max_records,
        max_chunk_bytes=65536,
    )

    print(f"[client] SubmitQuery: {req.query_text}")
    try:
        reply = stub.SubmitQuery(req)
        print(f"[client] QueryAccepted: req_id={reply.request_id} ({reply.message})")
    except grpc.RpcError as e:
        print(f"[client] SubmitQuery failed: {e.details()}")
        sys.exit(1)

    req_id = reply.request_id

    total_records = 0
    chunks = 0

    print(f"[client] polling FetchChunk for req_id={req_id}")
    while True:
        fetch_req = mini2_pb2.ChunkFetchRequest(
            client_id=client_id,
            request_id=req_id,
            max_records=max_records,
        )
        
        try:
            chunk = stub.FetchChunk(fetch_req)
        except grpc.RpcError as e:
            print(f"[client] FetchChunk failed: {e.details()}")
            break
            
        count = len(chunk.records)
        total_records += count
        chunks += 1
        
        print(f"  -> chunk {chunk.chunk_id}: {count} records, done={chunk.done}, cancelled={chunk.cancelled}")
        
        if chunk.done or chunk.cancelled:
            break

        # In A-only flow, the server has all results immediately, but we might want to sleep slightly
        # if we are polling. Since it's local, we can just fetch next immediately.
        # time.sleep(0.1)

    print(f"[client] finished. fetched {total_records} records across {chunks} chunks.")

if __name__ == "__main__":
    main()
