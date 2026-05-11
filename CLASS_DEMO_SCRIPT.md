# 🎤 CS 275: Mini 2 Final Presentation Script

This document is your exact battle-plan for the live demonstration. It is structured to systematically check off every requirement in the professor's `mini2-chunks.md` spec while keeping the presentation punchy, highly technical, and visually impressive.

---

## 🛠️ Pre-Demo Setup (Before the Prof walks over)
1. Ensure both MacBooks are connected to the exact same Wi-Fi network (or mobile hotspot).
2. On **Laptop 1 (Gateway)**, find its IP address (`ifconfig | grep inet`). Let's assume it's `192.168.1.10`.
3. Update `config/tree-lan/*.conf` so that all nodes point to the correct IPs.
4. Open your terminals:
   - **Laptop 1:** Have 2 terminal tabs open. One for running the backend, one for running the client.
   - **Laptop 2:** Have 1 terminal tab open.
5. Run the cleanup script on both laptops just to be safe: `bash scripts/kill_all.sh`

---

## 🎬 Phase 1: The Architecture Pitch (1 min)
*Goal: Prove you didn't hardcode anything, established the required Tree topology, and used two machines.*

**What you say:**
> *"Professor, for our Mini 2 implementation, we have deployed our distributed query engine across two physical machines over a WLAN. To ensure zero hardcoding, every node parses a dynamic configuration file on startup. We implemented the exact Tree overlay specified in the prompt, with Node A acting as the exclusive Gateway on Laptop 1, and the other nodes distributed across Laptop 1 and Laptop 2."*

**What you show:**
- Briefly `cat config/tree-lan/A.conf` to show the explicit neighbor mapping.

---

## 🚀 Phase 2: Polyglot Execution & Manual Chunking (1.5 mins)
*Goal: Prove you didn't cheat with gRPC streaming, prove you used typed data structures (not strings), and prove you used C++ and Python.*

**What you do:**
1. **Laptop 1:** `bash scripts/launch_all.sh` (Wait for it to say ready).
2. **Laptop 1:** Run a standard query using the C++ client:
   `./build/mini2_client 192.168.1.10:50051 borough eq MANHATTAN 1000`

**What you say:**
> *"I'm going to submit a query to the Gateway. As per your strict constraints, we completely avoided gRPC's native async/streaming APIs. Instead, the client polls the gateway using unary RPCs. The Gateway manually fragments the aggregated results into discrete payload chunks. You can see the chunk IDs scrolling in the logs now."*

> *"Also, notice that we are running the C++ Client here, communicating with the C++ Gateway. However, deep in the tree, Node I is written entirely in Python. Because we strictly adhered to strongly-typed data structures in our Protobuf definitions—using strict integers and doubles rather than lazy strings—the cross-language serialization is flawless."*

---

## ⚖️ Phase 3: Fairness & Adaptive Payloads (1.5 mins)
*Goal: Address the "fairness between end-points" and "optimize on the fly payload sizes" requirements.*

**What you do:**
1. **Laptop 1:** Run the fairness script:
   `python3 python/fairness_test.py 192.168.1.10:50051 1000`

**What you say:**
> *"A major challenge was fairness between concurrent requests. Large queries may produce thousands of result chunks, and if chunks are pushed greedily, one request can dominate the network and delay smaller requests. To address this, we implemented a custom round-robin scheduler at the chunk-dispatch stage. Instead of draining one request completely before serving another, each active request gets a turn to push one bounded chunk, which improves time-to-first-result and reduces starvation for smaller requests.."*

> *"Furthermore, to minimize network overhead, our ChunkManager uses Adaptive Chunking. It dynamically sizes the payload byte-budgets on the fly depending on network pressure, saving roughly 80% on RPC roundtrips."*

---

## 🔥 Phase 4: The "Wow Factor" Edge Cases (2 mins)
*Goal: Blow the professor away by answering his rhetorical questions from the prompt: Anticipation, Cancellations, and Failure Recovery.*

### Trick 1: Mid-Query Cancellation
**What you do:** Run `python3 python/cancel_test.py 192.168.1.10:50051 3`
**What you say:** 
> *"If a client abandons a connection, it shouldn't lock up the cluster. Here, we cancel a query mid-flight. Our system instantly broadcasts a `PeerCancel` wave down the tree, terminating worker threads across both machines to reclaim CPU."*

### Trick 2: Request Anticipation (The LRU Cache)
**What you do:** Run `bash scripts/test_cache.sh`
**What you say:** 
> *"In the spec, you asked the rhetorical question: 'Can requests be anticipated?' Yes, they can. We built an LRU Result Cache into the Gateway. When I run this script, it submits the exact same query twice. The first takes a fraction of a second to travel the cluster. The second query hits the LRU Cache and returns in exactly zero network hops. We completely anticipate and instantly fulfill redundant requests."*

### Trick 3: Failure Recovery (Optional Flex)
**What you say:**
> *"Finally, we added a Janitor thread with a per-child timeout. If one of our physical laptops were to crash mid-query or drop off the Wi-Fi, the gateway refuses to hang indefinitely. It will wait for a threshold, automatically sever the dead branch, and return a `partial=true` result set to the client."*

---

## 🏆 Checklist for the Professor
If he asks if you hit specific constraints, you can rapidly fire back:
- **No Hardcoding?** Yes, entirely config-driven.
- **No Streaming?** Yes, manual chunk tracking.
- **Tree Topology?** Yes, strict edge enforcements verified by cycle-guards.
- **Only A talks to Client?** Yes, peers use internal `PushPeerChunk` RPCs.
- **Typed structures?** Yes, Protobufs use `int64` and `double`.
- **Fairness?** Yes, Round-Robin scheduling.

*End the demo by offering to show him the C++ caching code or the Python routing logic if he wants to see the source!*
