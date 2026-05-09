#!/usr/bin/env python3
"""
server.py — Python gRPC server for Mini 2 leaf node I.

Step 7: Implements Heartbeat, PeerQuery, PushPeerChunk, PeerCancel.
SubmitQuery / FetchChunk / CancelQuery remain UNIMPLEMENTED (gateway-only,
and I is a leaf — clients must hit A).

Run: python3 python/server.py config/tree/I.conf
"""

import os
import sys
import time
import signal
import threading
from concurrent import futures

import grpc

# Add parent dir to path so we can import generated stubs and config_loader
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
GENERATED_DIR = os.path.join(SCRIPT_DIR, "generated")
sys.path.insert(0, GENERATED_DIR)
sys.path.insert(0, SCRIPT_DIR)

import mini2_pb2
import mini2_pb2_grpc
from config_loader import load_config
from datastore import DataStore, IndexSet, LocalQueryEngine


# ─── ActiveQuery state ───────────────────────────────────────────────────────

class ActiveQuery:
    """Per-request state on this node. I is a leaf, so expected_children
    is always empty and parent_node is always non-empty (set by the sender
    of the PeerQuery)."""
    __slots__ = ("request_id", "parent_node", "cancelled",
                 "local_done", "parent_done_sent", "last_activity")

    def __init__(self, request_id: str, parent_node: str):
        self.request_id = request_id
        self.parent_node = parent_node
        self.cancelled = False
        self.local_done = False
        self.parent_done_sent = False
        self.last_activity = time.time()


# ─── Service implementation ──────────────────────────────────────────────────

class Mini2ServiceImpl(mini2_pb2_grpc.Mini2ServiceServicer):
    def __init__(self, cfg, query_engine, stubs):
        self.cfg = cfg
        self.query_engine = query_engine
        self.stubs = stubs                   # dict[neighbor_id -> stub]
        self.lock = threading.Lock()
        self.active_queries: dict[str, ActiveQuery] = {}

    # ─── Health check ────────────────────────────────────────────────────

    def Heartbeat(self, request, context):
        now_ms = int(time.time() * 1000)
        print(f"[heartbeat] {self.cfg.node_id} received heartbeat from "
              f"{request.node_id} (ts={request.timestamp_ms})", flush=True)
        return mini2_pb2.HeartbeatReply(
            node_id=self.cfg.node_id,
            ok=True,
            timestamp_ms=now_ms,
        )

    # ─── External client RPCs — gateway-only, I always rejects ───────────

    def SubmitQuery(self, request, context):
        return mini2_pb2.QueryAccepted(
            request_id="",
            accepted=False,
            message="External queries must be sent to gateway A",
        )

    def FetchChunk(self, request, context):
        context.set_code(grpc.StatusCode.UNIMPLEMENTED)
        context.set_details("FetchChunk is gateway-only")
        return mini2_pb2.ChunkResponse()

    def CancelQuery(self, request, context):
        context.set_code(grpc.StatusCode.UNIMPLEMENTED)
        context.set_details("CancelQuery is gateway-only")
        return mini2_pb2.CancelAck()

    # ─── Internal peer RPCs ──────────────────────────────────────────────

    def PeerQuery(self, request, context):
        reply = mini2_pb2.PeerQueryAck(
            request_id=request.request_id,
            receiver_node=self.cfg.node_id,
        )

        # Visited check.
        if self.cfg.node_id in request.visited_nodes:
            reply.accepted = False
            reply.message = "already visited"
            return reply

        # Dedup: another path already registered this request_id here.
        with self.lock:
            if request.request_id in self.active_queries:
                reply.accepted = False
                reply.message = "duplicate path"
                return reply

            aq = ActiveQuery(request.request_id, request.sender_node)
            self.active_queries[request.request_id] = aq

        reply.accepted = True

        # I is a leaf — no neighbors to forward to (only neighbor is the sender).
        # Spawn worker thread to run local query and push results to parent.
        chunk_records = (request.chunk_records
                         if request.chunk_records > 0
                         else self.cfg.default_chunk_records)
        filter_copy = mini2_pb2.QueryFilter()
        filter_copy.CopyFrom(request.filter)

        threading.Thread(
            target=self._run_and_push,
            args=(request.request_id, filter_copy, request.sender_node, chunk_records),
            daemon=True,
        ).start()

        return reply

    def PushPeerChunk(self, request, context):
        # I is a leaf — should never receive PeerChunks from anyone.
        # Be defensive: accept gracefully so a misrouted chunk doesn't crash.
        return mini2_pb2.PeerChunkAck(
            request_id=request.request_id,
            receiver_node=self.cfg.node_id,
            accepted=False,
        )

    def PeerCancel(self, request, context):
        with self.lock:
            aq = self.active_queries.get(request.request_id)
            if aq:
                aq.cancelled = True
                print(f"[query] {self.cfg.node_id} peer-cancelled "
                      f"{request.request_id}", flush=True)
        return mini2_pb2.PeerCancelAck(
            request_id=request.request_id,
            cancelled=True,
        )

    # ─── Worker: run local query, push chunks to parent ──────────────────

    def _run_and_push(self, request_id: str, filter_msg, parent_node: str,
                      chunk_records: int):
        try:
            results = self.query_engine.run(filter_msg)
        except Exception as e:
            print(f"[query] {self.cfg.node_id} local query FAILED for "
                  f"{request_id}: {e}", flush=True)
            results = []

        # Cancellation check before pushing.
        with self.lock:
            aq = self.active_queries.get(request_id)
            if aq is None or aq.cancelled:
                return

        stub = self.stubs.get(parent_node)
        if stub is None:
            print(f"[query] {self.cfg.node_id} no stub for parent "
                  f"{parent_node}; dropping {request_id}", flush=True)
        else:
            chunk_id = 0
            i = 0
            n = len(results)
            while i < n:
                # Honor cancellation between chunks.
                with self.lock:
                    aq = self.active_queries.get(request_id)
                    if aq is None or aq.cancelled:
                        return

                end = min(i + chunk_records, n)
                chunk = mini2_pb2.PeerChunk(
                    request_id=request_id,
                    source_node=self.cfg.node_id,
                    destination_node=parent_node,
                    chunk_id=chunk_id,
                    source_done=False,
                )
                # results are already mini2_pb2.ServiceRequest protos.
                chunk.records.extend(results[i:end])
                try:
                    stub.PushPeerChunk(
                        chunk,
                        timeout=self.cfg.peer_timeout_ms / 1000.0,
                    )
                except grpc.RpcError as e:
                    print(f"[query] {self.cfg.node_id} push to "
                          f"{parent_node} FAILED: {e.code()}", flush=True)
                    return
                i = end
                chunk_id += 1

        # Mark local done and emit source_done exactly once.
        with self.lock:
            aq = self.active_queries.get(request_id)
            if aq is None or aq.cancelled or aq.parent_done_sent:
                return
            aq.local_done = True
            aq.parent_done_sent = True
            target_parent = aq.parent_node

        if stub is not None:
            done_chunk = mini2_pb2.PeerChunk(
                request_id=request_id,
                source_node=self.cfg.node_id,
                destination_node=target_parent,
                chunk_id=0,
                source_done=True,
            )
            try:
                stub.PushPeerChunk(
                    done_chunk,
                    timeout=self.cfg.peer_timeout_ms / 1000.0,
                )
                print(f"[query] {self.cfg.node_id} emitted source_done -> "
                      f"{target_parent} for {request_id} ({len(results)} records)",
                      flush=True)
            except grpc.RpcError as e:
                print(f"[query] {self.cfg.node_id} source_done to "
                      f"{target_parent} FAILED: {e.code()}", flush=True)


# ─── Heartbeat sender ───────────────────────────────────────────────────────

def heartbeat_loop(cfg, stubs, shutdown_event):
    while not shutdown_event.is_set():
        now_ms = int(time.time() * 1000)
        for neighbor in cfg.neighbors:
            try:
                stub = stubs[neighbor.id]
                req = mini2_pb2.HeartbeatRequest(
                    node_id=cfg.node_id,
                    timestamp_ms=now_ms,
                )
                reply = stub.Heartbeat(req, timeout=cfg.peer_timeout_ms / 1000.0)
                print(f"[heartbeat] {cfg.node_id} -> {neighbor.id} OK "
                      f"(reply_node={reply.node_id}, reply_ts={reply.timestamp_ms})",
                      flush=True)
            except Exception as e:
                print(f"[heartbeat] {cfg.node_id} -> {neighbor.id} FAILED: {e}",
                      flush=True)
        for _ in range(20):
            if shutdown_event.is_set():
                break
            time.sleep(0.1)


# ─── Main ────────────────────────────────────────────────────────────────────

def main():
    if len(sys.argv) < 2:
        print("usage: python3 server.py <config-path>", file=sys.stderr)
        sys.exit(1)

    cfg = load_config(sys.argv[1])

    print(f"[mini2_server] node_id={cfg.node_id} role={cfg.role} "
          f"host={cfg.host} port={cfg.port} "
          f"dataset={cfg.dataset_path} neighbors={len(cfg.neighbors)} "
          f"overlay={cfg.overlay}", flush=True)
    for n in cfg.neighbors:
        print(f"  -> {n.id} @ {n.host}:{n.port}", flush=True)

    # Load shard + indexes
    store = DataStore()
    store.load(cfg.dataset_path)
    index = IndexSet()
    index.build(store)
    query_engine = LocalQueryEngine(store, index)

    # Create stubs for neighbors first — service needs them for chunk pushes
    stubs = {}
    for neighbor in cfg.neighbors:
        target = f"{neighbor.host}:{neighbor.port}"
        channel = grpc.insecure_channel(
            target,
            options=[
                ("grpc.max_receive_message_length", 16 * 1024 * 1024),
                ("grpc.max_send_message_length", 16 * 1024 * 1024),
            ],
        )
        stubs[neighbor.id] = mini2_pb2_grpc.Mini2ServiceStub(channel)
        print(f"[client_pool] {cfg.node_id} created stub for {neighbor.id} @ {target}",
              flush=True)

    # gRPC server
    server = grpc.server(
        futures.ThreadPoolExecutor(max_workers=cfg.worker_pool_size),
        options=[
            ("grpc.max_receive_message_length", 16 * 1024 * 1024),
            ("grpc.max_send_message_length", 16 * 1024 * 1024),
        ],
    )
    service = Mini2ServiceImpl(cfg, query_engine, stubs)
    mini2_pb2_grpc.add_Mini2ServiceServicer_to_server(service, server)

    addr = f"{cfg.host}:{cfg.port}"
    bound = server.add_insecure_port(addr)
    if bound == 0:
        print(f"[grpc] FATAL: failed to bind {addr} (port in use?)",
              file=sys.stderr, flush=True)
        sys.exit(2)
    server.start()
    print(f"[grpc] server {cfg.node_id} listening on {addr}", flush=True)

    shutdown_event = threading.Event()
    hb_thread = threading.Thread(
        target=heartbeat_loop,
        args=(cfg, stubs, shutdown_event),
        daemon=True,
    )
    hb_thread.start()

    print(f"[mini2_server] {cfg.node_id} running. Press Ctrl+C to stop.",
          flush=True)

    # Robust shutdown: signal handlers set the event; main thread polls it.
    # This avoids the SIGTERM-stuck-in-wait_for_termination problem.
    def handle_signal(signum, frame):
        print(f"\n[mini2_server] {cfg.node_id} received signal {signum}, "
              f"shutting down...", flush=True)
        shutdown_event.set()

    signal.signal(signal.SIGINT, handle_signal)
    signal.signal(signal.SIGTERM, handle_signal)

    try:
        while not shutdown_event.is_set():
            time.sleep(0.2)
    finally:
        server.stop(grace=2).wait(timeout=5)
        hb_thread.join(timeout=3)
        print(f"[mini2_server] {cfg.node_id} stopped.", flush=True)


if __name__ == "__main__":
    main()
