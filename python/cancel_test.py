#!/usr/bin/env python3
import sys
import os
import time
import grpc

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(SCRIPT_DIR, "generated"))

import mini2_pb2
import mini2_pb2_grpc

def main():
    channel = grpc.insecure_channel("127.0.0.1:50051")
    stub = mini2_pb2_grpc.Mini2ServiceStub(channel)

    client_id = f"cancel-client"
    filter_msg = mini2_pb2.QueryFilter(field_name="borough", op="eq", value="MANHATTAN")
    req = mini2_pb2.QueryRequest(
        client_id=client_id, query_text="Cancel me", filter=filter_msg, requested_chunk_records=500
    )

    reply = stub.SubmitQuery(req)
    req_id = reply.request_id
    print(f"Submitted query. req_id={req_id}")

    # Fetch 2 chunks
    for _ in range(2):
        chunk = stub.FetchChunk(mini2_pb2.ChunkFetchRequest(client_id=client_id, request_id=req_id, max_records=500))
        print(f"Fetched chunk {chunk.chunk_id}, done={chunk.done}, cancelled={chunk.cancelled}")

    # Cancel
    print("Cancelling...")
    cancel_ack = stub.CancelQuery(mini2_pb2.CancelRequest(client_id=client_id, request_id=req_id, reason="testing"))
    print(f"CancelAck: cancelled={cancel_ack.cancelled}")

    # Fetch again, should get cancelled=True
    chunk = stub.FetchChunk(mini2_pb2.ChunkFetchRequest(client_id=client_id, request_id=req_id, max_records=500))
    print(f"Fetched chunk {chunk.chunk_id}, done={chunk.done}, cancelled={chunk.cancelled}")

if __name__ == "__main__":
    main()
