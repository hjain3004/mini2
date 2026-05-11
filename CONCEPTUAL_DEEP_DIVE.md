# 🧠 CS 275: Mini 2 Conceptual Deep-Dive & Study Guide

This document is your "brain dump." It explains **how** and **why** your system works at a deep architectural level. If you memorize the concepts in this guide, no question the professor asks can trip you up, because you will understand the underlying mechanics of your distributed query engine.

---

## 1. The Core Philosophy
The entire purpose of this project is **Temporal and Spatial Decoupling**.
- **Spatial Decoupling:** The data is not in one place. It is scattered across 9 shards across 2 physical machines.
- **Temporal Decoupling:** We don't return all 4 million records at once. We delay the data transfer, trickling it back to the client "on demand" (chunking) to conserve memory and bandwidth.

---

## 2. The Architectural Constraints (And Why They Matter)

Your professor imposed strict guardrails. Understanding *why* he did this is crucial:
1. **No gRPC Streaming APIs:** gRPC has built-in streaming that automatically chunks data. We were forbidden from using it because it hides the control flow. By forcing us to use unary (one-off) RPCs, we had to manually build a Chunk Manager, track offsets, and implement our own fairness scheduler.
2. **No Dynamic Discovery:** Nodes don't use a gossip protocol to find each other. They use a strict static `.conf` file. This forced us to intentionally route messages through a specific Tree Overlay (AB, BC, BD, etc.) rather than just broadcasting to everyone.
3. **No Raw Strings:** We used strict `int64` and `double` types in our Protobufs. Why? Because serializing and deserializing raw strings is incredibly CPU-intensive. Real databases use binary types. Furthermore, it allowed our Polyglot system (C++ and Python) to communicate natively without writing custom string parsers in both languages.

---

## 3. Anatomy of a Query (The Scatter-Gather Flow)

If the professor asks: *"Walk me through exactly what happens when a client asks for all records in Manhattan,"* here is the flow:

1. **Submission:** The Client sends a `SubmitQuery` RPC to Node A (The Gateway).
2. **Registration:** Node A generates a unique `request_id`, instantiates an `ActiveQuery` struct in memory, and immediately replies to the client with `accepted=true`.
3. **The Scatter (Forwarding):** Node A looks at its `.conf` file and forwards the query to its neighbors (B, G, H, I) using `PeerQuery`. 
   - *Crucial Detail:* To prevent infinite routing loops in the tree, every time a node forwards a query, it appends its own ID to a `visited_nodes` array. If Node D receives a query and sees "D" in that array, it drops it.
4. **The Gather (Pushing Chunks):** When a leaf node (like C) finds matching records using its local Index, it doesn't wait. It immediately packages them into a `PeerChunk` and pushes them back up to B. B pushes them to A.
5. **Polling:** Meanwhile, the Client is constantly calling `FetchChunk`. Node A pops the aggregated records off its internal queue, strictly abiding by a byte budget, and returns them to the client.

---

## 4. Memory Management & Adaptive Chunking

If the professor asks: *"How do you conserve memory when returning 4 million records?"*

**The Dual-Constraint System:**
We never load 4 million records into a single gRPC message. Instead, our `ChunkManager` uses a dual-constraint system:
1. **Max Record Limit:** e.g., 5,000 records max.
2. **Strict Byte Budget:** e.g., 65 KB max.

**The byte-budget safety net (always on):**
If a record has a very long text description, 5,000 records might be 50 MB, which would blow up the network buffers. Our `ChunkManager` is hard-bounded by the 65 KB byte budget: if the budget is hit at 300 records, it truncates the chunk right there and sends it. This guards against OOM crashes regardless of mode.

**Adaptive chunking (the latency-driven optimizer):**
On top of the byte budget, adaptive mode tunes `chunk_records` based on observed `PushPeerChunk` round-trip latency: if latency < 20ms and the queue is deep, it **doubles** the record cap (capped at 5,000); if latency > 200ms, it **halves** it (floored at 100). The cluster converges quickly to the largest viable chunks under good network conditions, yielding 83% fewer chunk-pushes and 13% lower total time on 3.7M-record queries.

---

## 5. Fairness & The Round-Robin Scheduler

If the professor asks: *"How did you achieve fairness between end-points?"*

**The Problem (Starvation):**
Imagine a queue. Query 1 asks for all of Manhattan (~4M records). Query 2 asks for a small slice (~338K records — "Noise - Vehicle"). Under Greedy, the worker keeps dispatching chunks for Query 1 until it has nothing ready, only then rotating to Query 2 — so Query 2's total completion time stretches to 1.29s (vs. 0.89s under Round-Robin). The smaller the query, the worse the relative penalty: small queries "starve" against background bulk traffic.

**The Solution (Round-Robin):**
We implemented a Round-Robin Scheduler. The Gateway loops through all active queries sequentially. 
- It gives Query 1 a single chunk (e.g., 1000 records). 
- Then it pauses Query 1 and gives Query 2 its single chunk. 
- Because Query 2 only needed 1 chunk, it finishes instantly!
- The Gateway then goes back and continues feeding Query 1.
*This mathematically eliminates starvation and is why our small queries showed a 31% speedup.*

---

## 6. The "Wow Factor" Mechanics

### Request Anticipation (The LRU Cache)
*How it works:* We built an LRU (Least Recently Used) cache in the Gateway. The cache key is the exact binary serialization of the `QueryFilter` protobuf message. When the Gateway finishes a query, it wraps the entire massive result vector in a `std::shared_ptr` and stores it in the cache. If a second client asks the exact same question, the Gateway detects a cache hit, bypasses the network entirely, and streams the `shared_ptr` data directly to the client.

### Mid-Flight Cancellation
*How it works:* If a client disconnects, we shouldn't waste CPU processing 4 million records. The Gateway sends a `PeerCancel` RPC. Every node that receives it flips an atomic boolean flag (`aq.cancelled = true`), immediately halts its worker threads, and recursively forwards the cancel command to its children.

### Failure Recovery (The Janitor Thread)
*How it works:* We have a background "Janitor" thread that wakes up every 500ms. It scans all active queries. We implemented a `peer_completion_timeout` (e.g., 30 seconds). If Node A is waiting for Node B, but Node B's laptop crashed, the Janitor detects the timeout. It forcefully severs Node B from the expected children list and returns a `partial=true` warning to the client. This ensures the system remains highly available even if the network degrades.
