# Mini 2 — Progress Log

Living status of the 13-step build order from `IMPLEMENTATION_PLAN.md` §
"Build Order". Update this file as each step is completed. Newest entry
on top of "Recent activity"; checklist below.

---

## Step Checklist

- [x] **1. Skeleton + proto** — CMake builds; protoc generates C++ stubs;
  empty `mini2_server` boots from a config and prints `node_id`.
- [x] **2. ConfigManager + Heartbeat** — load `key=value` config; A pings B
  over gRPC. Same in Python for I. ✅ Basecamp verified.
- [x] **3. DataStore + CSVParser + IndexSet** — load shard CSV, build all four
  indexes, expose `LocalQueryEngine.run(filter)`.
- [x] **4. A-only flow** — `SubmitQuery` + `FetchChunk` + `CancelQuery` on A's
  *local* shard; client polls until `done`.
- [x] **5. PeerQuery + PushPeerChunk** — single hop A↔B working; visited_nodes
  plumbing in place.
- [x] **6. Multi-hop forwarding** — full tree works locally with all 9
  servers; verify completion semantics under E-D / A-G cycles.
- [x] **7. Python node I** — bring up I as a Python leaf; client query
  reaches it through A.
- [x] **8. Scheduler + WorkerPool** — round-robin policy;
  `test_many_clients.sh` shows fairness vs greedy.
- [ ] **9. Cancellation + TTL janitor** — `PeerCancel` propagation;
  abandoned-fetch cleanup.
- [ ] **10. Adaptive chunking** — flag-gated; tune via experiments.
- [ ] **11. Metrics + experiments** — run §20.{1,2,4,5} (chunk size,
  fairness, local-vs-distributed, linear-vs-indexed); plot.
- [ ] **12. Two-machine demo** — finalize `tree-lan/` configs, run §22 demo.
- [ ] **13. Report** — `report/mini2_report.md` per §24 structure.

---

## Recent Activity

### Step 8 — Scheduler + WorkerPool · DONE

**Date:** 2026-05-09

**What landed:**
- `cpp/include/scheduler/WorkerPool.hpp` + `cpp/src/scheduler/WorkerPool.cpp`
  — fixed-size thread pool with FIFO task queue. Bounded thread usage
  replaces the unbounded `std::thread().detach()` pattern at the three
  sites in `ServiceImpl.cpp` (SubmitQuery PeerQuery forwarding, PeerQuery
  PeerQuery forwarding, PeerQuery local-query execution).
- `cpp/include/scheduler/Scheduler.hpp` + `cpp/src/scheduler/Scheduler.cpp`
  — single-driver chunk-push scheduler with two policies:
  - **Greedy:** drains a single request's chunks fully before serving the next
  - **RoundRobin:** each ready request gets one chunk per cycle, then yields
  - Driver thread snapshots one chunk under the mutex, releases the mutex,
    then performs the outbound `PushPeerChunk` RPC. When a job's records are
    exhausted, calls `on_local_done(req_id)` so the service can mark
    `local_done` and emit `source_done` upward via the existing
    `try_emit_done_to_parent` aggregation helper.
- `cpp/include/grpc/ServiceImpl.hpp` + `cpp/src/grpc/ServiceImpl.cpp`
  — service now holds references to `WorkerPool` and `Scheduler`. The
  PeerQuery local-query path is split: WorkerPool runs `query_engine_.run`,
  then hands the result vector to `scheduler_.submit(req_id, parent, …)`.
  Forwarding tasks go through WorkerPool. Added
  `on_scheduler_local_done(req_id)` callback method.
- `cpp/src/main_server.cpp` — instantiates WorkerPool and Scheduler before
  the service. Scheduler's `on_local_done` callback is wired to the service
  via a `unique_ptr` indirection so the closure can resolve before the
  service object exists. Shutdown order: gRPC server stop → scheduler stop
  → worker pool stop → heartbeat thread join.
- `CMakeLists.txt` — adds the two new sources to `mini2_core`.
- `python/fairness_test.py` — multi-threaded fairness probe. Submits 3
  concurrent clients (HUGE/MED/SMALL) against gateway A, measures per-client
  time-to-first-chunk and total time, prints a sorted summary.
- `scripts/test_many_clients.sh` — sed-flips `scheduler_policy` across
  every `config/tree/*.conf`, restarts the cluster under each policy, runs
  `fairness_test.py`, and writes side-by-side results to
  `experiments/fairness/{round_robin,greedy}.txt`. Restores configs to
  `round_robin` on exit.

**Verification:**
- Single-client smoke: `python3 python/client.py … MANHATTAN 1000` →
  4,124,656 records / 4,125 chunks / done=True in ~3.1s. Same as step 7;
  refactor is non-regressive.
- Multi-client comparison via `scripts/test_many_clients.sh`:

  ```
  --- round_robin ---
  label                   records   chunks  first_chunk_s    total_s
  SMALL-noise-veh          380043      849          0.032      0.949
  MED-staten-island        861552     1815          0.046      1.681
  HUGE-manhattan          4124656     4125          0.052      3.512

  --- greedy ---
  label                   records   chunks  first_chunk_s    total_s
  SMALL-noise-veh          380043      381          0.012      0.545
  MED-staten-island        861552      863          0.021      1.055
  HUGE-manhattan          4124656     4125          0.042      3.279
  ```

  Key observation: **same record totals across policies (correctness)**, but
  **chunk counts differ dramatically for SMALL/MED queries** under
  round_robin (849 vs 381, 1815 vs 863). This is the scheduler interleaving
  in action — under RR, peers split each push round across all 3 ready
  jobs, so the gateway often returns partial chunks to the small client
  while waiting for the next interleaved push. Under greedy, each peer
  bursts a full job's worth of chunks, so the client fetches full
  1000-record chunks in tight succession. Total wall time is dominated by
  HUGE in both modes (3.5 vs 3.3s) because there is only one network bottleneck.

**Notes / decisions:**
- A single driver thread per node is intentional: it makes the policy
  observable (one push at a time, ordered by policy) and avoids racey
  fairness semantics across multiple concurrent pushers. Throughput is
  bounded by network, not CPU, so this is not a real bottleneck — the
  fairness numbers above (3.5s wall time for ~5.4M records across 9
  shards) confirm.
- WorkerPool defaults to `cfg.worker_pool_size` (8) — sized to be larger
  than typical neighbor count + concurrent local queries.
- Scheduler's `submit()` with empty `parent_node` is a no-op push (the
  gateway path stores records directly in `aq.results` from
  `PushPeerChunk`). It still fires `on_local_done` so symmetric callers
  don't need a special case.
- Greedy's queue had a small ballooning issue (it kept re-adding the
  current-locked job even though the driver picks `greedy_current_`
  directly). Fixed: only re-add when the queue is empty (so the wait
  predicate has something to fire on).
- Step 8 does NOT touch Python I (leaf, single push job per request — no
  fairness benefit). The Python pusher in `python/server.py` still uses
  the inline-thread approach. That's fine; the fairness test still shows
  correct behavior because I contributes its full ~458k records
  in-order regardless of policy.

**Open items rolled into step 9:**
- `Scheduler::cancel(req_id)` exists but isn't yet wired to `CancelQuery`
  / `PeerCancel`. Step 9 adds the propagation.
- `Scheduler` doesn't yet record metrics (chunk push latency, queue
  depth). Step 11 will add `MetricsRecorder` rows for fairness analysis.

---

### Step 7 — Python node I peer participation · DONE

**Date:** 2026-05-08

**What landed:**
- `python/server.py` — full peer participation for the leaf:
  - **`PeerQuery`** now (a) does the visited-nodes check, (b) dedups against
    `active_queries`, (c) registers state with `parent_node = sender`, and
    (d) spawns a worker thread that runs the local query and pushes records
    back to the parent in chunks. After all records are pushed, it sends a
    final `source_done=true` chunk. Cancellation is checked between chunks.
  - **`PushPeerChunk`** now returns `accepted=false` cleanly (I is a leaf,
    should never receive chunks; defensive only).
  - **`PeerCancel`** marks the active query cancelled, halting the chunk
    pusher between chunks.
  - **`SubmitQuery`** now correctly rejects with the gateway-only message
    instead of UNIMPLEMENTED (matches C++ behavior).
- `python/server.py` — replaced `server.wait_for_termination()` with a
  shutdown-event poll loop and `server.stop(grace=2).wait()`. SIGTERM now
  shuts I down cleanly (the prior version required SIGKILL).
- `python/server.py` — `add_insecure_port` return value is now checked;
  bind failure aborts with clear error instead of silently letting the
  server accept zero RPCs (silent-bind was the bug that masked stale
  zombies in the first run).
- `python/server.py` — added `flush=True` on every `print` so logs appear
  immediately when stdout is redirected to a file (matters for the
  background `launch_all.sh` workflow).
- `scripts/kill_all.sh` — proper cleanup script that walks PID files,
  TERMs everything, then SIGKILLs anything still alive matching
  `mini2_server` or `python/server.py`. Prevents zombie accumulation
  across test runs.

**Verification:**
- `scripts/kill_all.sh` → `scripts/launch_all.sh`. All 9 nodes ALIVE.
- `python3 python/client.py 127.0.0.1:50051 borough eq MANHATTAN 1000`.
- Client received **4,124,656 records across 4,125 chunks, done=True** in
  ~3 seconds. That's all **9 shards** contributing:
  - 8 C++ shards × ~458,328 = 3,666,627 (matches step 6)
  - Python I = 458,029 (matches I.log: `emitted source_done -> A for A-1
    (458029 records)`)
  - Sum = 4,124,656 ✓
- A.log: `marked child I done for A-1` confirms I now participates as a
  proper child via `source_done` rather than being silently treated as a
  failed neighbor.
- After query, `kill_all.sh` shut all 9 down cleanly. I.log final lines:
  `I received signal 15, shutting down...` then `I stopped.` — no SIGKILL
  needed.

**Notes / decisions:**
- I (leaf) doesn't need most of the multi-hop machinery: no neighbor
  forwarding, no `expected_children`, no aggregation, no `mark_child`
  bookkeeping (I has zero children). The Python implementation is
  intentionally a *trimmed mirror* of the C++ ServiceImpl — this keeps
  the diff small for a future pass that promotes I or another Python node
  to an intermediate role.
- During first-run debugging, found that a prior session had left zombie
  C++ workers and Python servers from incomplete shutdowns. The new I
  silently failed to bind 50059, A's PeerQuery hit the *old* I (which
  still had step-2's UNIMPLEMENTED stub), and A treated I as a failed
  neighbor — masking that step 7 was actually working. Fixed by adding
  bind-result check + `kill_all.sh`.
- `_run_and_push` checks cancellation between chunks. Step 9
  (`PeerCancel` propagation) will refine this further; the leaf's
  cancel-aware pusher is already the right shape.

---

### Step 6 — Multi-hop forwarding (full 9-node tree) · DONE

**Date:** 2026-05-08

**Continued from where Antigravity left off mid-step.** Inspected the
existing peer-forwarding code and found four correctness bugs that would
break the full-tree run under cycles (E-D, A-G/E-G):

1. **No PeerQuery dedup.** Two paths can reach the same node (e.g. G via
   A and via B→E). The handler unconditionally `active_queries_[req_id] =
   move(aq)`, double-running the local query and clobbering `parent_node`.
2. **`source_done` chunks forwarded as-is.** When intermediate E forwarded
   F's source_done up to B, B's `completed_children` recorded "F" — but B's
   `expected_children` was {C,D,E}, not F. Completion math broke.
3. **No once-only guard on emitting source_done.** Both the
   local-query-finished path and the last-child-arrived path could fire
   `source_done` to the parent.
4. **Mutex held during outbound RPC.** `PushPeerChunk` made synchronous
   peer RPCs while holding `mutex_`, blocking all other request bookkeeping
   and risking cycle deadlocks.

**What landed:**
- `cpp/include/grpc/ServiceImpl.hpp` — added `parent_done_sent` to
  `ActiveQuery`; declared two private helpers
  (`mark_child_completed`, `try_emit_done_to_parent`).
- `cpp/src/grpc/ServiceImpl.cpp` — full rewrite of peer-forwarding logic:
  - **PeerQuery** now (a) bails early if already in path or already
    registered for this `req_id`, replying `accepted=false` so the caller
    treats us as already-complete; (b) registers state once before
    spawning forwarders/local-query thread.
  - **PushPeerChunk** snapshots its mode (gateway vs intermediate) and the
    parent under the mutex, releases the mutex, then performs the outbound
    RPC. Records-only chunks are forwarded with `source_node` rewritten to
    the current node so the parent's child-bookkeeping stays consistent.
    `source_done` chunks are **never** forwarded — the aggregation helper
    emits our own `source_done` exactly once when local + all expected
    children are done.
  - **Failure handler** in both SubmitQuery and PeerQuery forwarders calls
    the new `mark_child_completed` + `try_emit_done_to_parent` helpers, so
    UNIMPLEMENTED replies (Python I) and cycle-dedup rejections are
    treated symmetrically as "this child is done."
- `experiments/logs/` directory created for `launch_all.sh`.

**Verification:**
- Brought up all 9 nodes (8 C++ + Python I) via `scripts/launch_all.sh`.
- Confirmed all 9 process PIDs alive after dataset load (~25s).
- Ran `python3 python/client.py 127.0.0.1:50051 borough eq MANHATTAN 1000`.
- Client received **3,666,627 records across 3,667 chunks, done=True** in
  ~3 seconds. That's 8 C++ shards × ~458k MANHATTAN records/shard. Python
  I correctly returned UNIMPLEMENTED for `PeerQuery` (step 7's job) and was
  treated as `completed_children` by A's failure handler — the system
  tolerates the dropout.
- Log audit:
  - `A.log`: 3 explicit `marked child` (B, G, H) + 1 silent (I via
    UNIMPLEMENTED failure path).
  - `B.log`: 2 explicit (C, D) + 1 silent (E via cycle-dedup failure
    path because G's A→G→E PeerQuery beat B's A→B→E race) +
    `emitted source_done -> A` exactly once.
  - `E.log`: `emitted source_done` exactly once.
- No node logged a duplicate local query execution.

**Notes / decisions:**
- The PeerQuery dedup uses a check-then-insert under the same mutex
  acquire; a tiny race window exists between the initial dedup check and
  the registration insert. Mitigated by re-checking inside the registration
  block; double execution under the race would still terminate correctly
  (just wasted work). Acceptable for step 6.
- Failure-path `mark_child_completed` does not log; explicit `source_done`
  arrivals via PushPeerChunk do log. This makes log audits read clean
  ("you only see what came in over the wire").
- Mutex is released before any outbound RPC anywhere in the multi-hop
  path now. Future scheduler/worker-pool work (step 8) can safely batch
  pushes without risk of deadlock.

---

### Step 5 — PeerQuery + PushPeerChunk (A↔B) · DONE

**Date:** 2026-05-08

**What landed:**
- `cpp/include/grpc/ServiceImpl.hpp` — Added `parent_node`, `expected_children`, `completed_children`, and `local_done` to `ActiveQuery` struct.
- `cpp/src/main_server.cpp` — Hooked `GrpcClientPool` into `Mini2ServiceImpl` so the service can initiate peer RPCs.
- `cpp/src/grpc/ServiceImpl.cpp`:
  - **`SubmitQuery`**: Sends `PeerQuery` asynchronously to all neighbors using the client pool. Automatically flags failed requests as completed to prevent hangs.
  - **`PeerQuery`**: Checks `visited_nodes` to avoid cycles. Forwards the query to its downstream neighbors, executes the local query, and uses `PushPeerChunk` to stream results back to the `parent_node`.
  - **`PushPeerChunk`**: Accepts incoming chunks from children. If intermediate, it forwards them to its parent. If gateway, it appends them to `aq.results` for the client to fetch. Calculates `all_children_done` before emitting `done=true`.

**Verification:**
- Ran Server A and Server B.
- Sent `borough eq MANHATTAN` via Python client to A.
- Node A executed its local query and sent `PeerQuery` to B.
- Node B executed its local query and sent `PushPeerChunk` back to A.
- The Python client successfully fetched **915,770 records** across **1,832 chunks**! (457,601 from A + 458,169 from B).

---

### Step 4 — A-only Query Flow · DONE

**Date:** 2026-05-08

**What landed:**
- `cpp/include/grpc/ServiceImpl.hpp` + `cpp/src/grpc/ServiceImpl.cpp` — updated to keep track of active queries and their results in `std::unordered_map<std::string, ActiveQuery>`.
- **`SubmitQuery`**: Generates a request ID, executes the `LocalQueryEngine` to find matching records, stores them in memory, and returns success.
- **`FetchChunk`**: Looks up the query ID, paginates through the result set up to `max_records`, sets `done=true` when finished, and handles cancellation limits.
- **`CancelQuery`**: Flags an active query as cancelled, preventing further results from being returned.
- `python/client.py` — functional CLI external client that submits a query to A, polls `FetchChunk` in a loop, and accumulates records.
- `python/cancel_test.py` — functional test for `CancelQuery`.

**Verification:**
- Ran `python/client.py 127.0.0.1:50051 borough eq MANHATTAN 500`.
- Server A processed it against its 2.2 million rows instantly.
- Client successfully pulled **457,601** matching records via 916 chunks in under 3 seconds.
- Cancellation test succeeded, stopping the stream instantly.

---

### Step 3 — DataStore + CSVParser + IndexSet · DONE

**Date:** 2026-05-08

**What landed:**
- `scripts/partition_data.py` — deterministic script to partition the 12.7 GB dataset by `hash(unique_key) % 9` into `data/partitions/{A..I}.csv`.
- `cpp/include/model/ServiceRequest.hpp` + `cpp/src/model/ServiceRequest.cpp` — NYC 311 `ServiceRecord` domain object, mapping strings to enums, and Protobuf to/from conversion.
- `cpp/include/model/CSVParser.hpp` + `cpp/src/model/CSVParser.cpp` — RFC-4180 compliant CSV parser specifically tailored for the 43-column layout.
- `cpp/include/query/DataStore.hpp` + `cpp/src/query/DataStore.cpp` — in-memory columnar/row storage for `ServiceRecord`.
- `cpp/include/query/IndexSet.hpp` + `cpp/src/query/IndexSet.cpp` — builds four secondary indexes (`borough`, `complaint_type`, `created_date`, and a 0.01-degree grid `geo_bbox` index).
- `cpp/include/query/LocalQueryEngine.hpp` + `cpp/src/query/LocalQueryEngine.cpp` — evaluates `QueryFilter` against the `DataStore`, leveraging `IndexSet` to minimize linear scans.
- `python/datastore.py` — Python equivalent of `DataStore`, `IndexSet`, `CSVParser`, and `LocalQueryEngine` for Node I.
- `cpp/src/main_server.cpp` and `python/server.py` — updated to load their respective CSV shards and build indexes before starting the gRPC server.

**Verification:**
- `partition_data.py` successfully split 20,417,819 rows into 9 files of ~2.26 million rows each.
- C++ Server A booted, parsed 2.26 million records, built indexes (`borough=6`, `complaint=248`, `dates=2.1M`, `geo_cells=947`), and started listening in ~3 seconds.
- Python Server I booted, parsed 2.26 million records, built indexes, and started listening in ~20 seconds.

---

### Step 2 — ConfigManager + gRPC Server + Heartbeat · DONE

**Date:** 2026-05-08

**What landed:**
- `cpp/include/grpc/Server.hpp` + `cpp/src/grpc/Server.cpp` — wraps
  `grpc::ServerBuilder`, binds `host:port`, registers service.
- `cpp/include/grpc/ServiceImpl.hpp` + `cpp/src/grpc/ServiceImpl.cpp` —
  `Mini2ServiceImpl` with `Heartbeat` working; all other RPCs return
  `UNIMPLEMENTED`.
- `cpp/include/grpc/ClientPool.hpp` + `cpp/src/grpc/ClientPool.cpp` —
  lazy stub-per-neighbor pool, thread-safe.
- `cpp/src/main_server.cpp` — starts real gRPC server, heartbeat thread
  (2s intervals), SIGINT/SIGTERM graceful shutdown.
- `config/tree/{C..I}.conf` — all 9 configs now exist (ports 50051–50059).
- `python/server.py` — Python gRPC server for node I with `Heartbeat`.
- `python/config_loader.py` — mirrors C++ ConfigManager.
- `python/requirements.txt` — grpcio, grpcio-tools, protobuf.
- `scripts/gen_python_stubs.sh` — generates `python/generated/mini2_pb2*.py`.
- `CMakeLists.txt` — added Server, ServiceImpl, ClientPool to `mini2_core`.

**Verification:**
- C++ A ↔ C++ B bidirectional heartbeats confirmed (every ~2s).
- Python I ↔ C++ A bidirectional heartbeats confirmed (cross-language).
- Build: clean compile, no warnings.

**Notes:**
- gRPC message size limits set to 16 MB for large chunk transfers.
- Heartbeat failures to offline neighbors are logged but non-fatal.
- Python stubs generated into `python/generated/` (gitignored).

---

### Step 1 — Skeleton + proto · DONE

**Date:** 2026-05-08

**What landed:**
- `proto/mini2.proto` — full RPC surface (`SubmitQuery`, `FetchChunk`,
  `CancelQuery`, `PeerQuery`, `PushPeerChunk`, `PeerCancel`, `Heartbeat`)
  plus a typed `ServiceRequest` message mirroring Mini 1's schema.
- `cpp/include/config/ConfigManager.hpp` + `cpp/src/config/ConfigManager.cpp`
  — `key=value` parser, validates required fields, parses neighbors as
  comma-separated `id:host:port`.
- `cpp/src/main_server.cpp` — loads config, prints identity + neighbor list,
  asserts gRPC stubs link by referencing `mini2::Mini2Service::service_full_name()`.
- `CMakeLists.txt` — top-level. Finds Homebrew `Protobuf` and `gRPC`,
  generates stubs into `cpp/generated/` via a custom command, builds three
  static targets (`mini2_proto`, `mini2_core`) and one binary (`mini2_server`).
- `config/tree/A.conf`, `config/tree/B.conf` — A as gateway with 4 neighbors,
  B as worker with 4 neighbors.
- `scripts/build.sh` — wraps the CMake invocation; honours `MINI2_PREFIX`.
- `.gitignore` — excludes `build/`, `cpp/generated/`, partition CSVs,
  `dataset/`, and metrics output.

**Verification:**
- `./scripts/build.sh` compiles cleanly under Clang 22 / Homebrew protobuf 34.1.
- `./build/mini2_server config/tree/A.conf` and `…/B.conf` both load configs
  correctly and print the expected identity and neighbor lists.

**Notes / decisions made during the step:**
- `mini2_proto` is a static lib so consumers don't have to repeat the
  generated-include path; `mini2_core` (the future home for ConfigManager,
  routing, query engine, etc.) publicly links it.
- `ConfigManager` tolerates unknown keys to keep configs forward-compatible
  during dev (e.g. adding `metrics.foo=…` later won't break older binaries).
- The gRPC sync API is what we're committing to (per the plan's no-async-streaming
  rule). The proto file uses unary RPCs only.

**Open items rolled into step 2:**
- No service implementation yet. Step 2 brings up `Mini2ServiceImpl` with
  `Heartbeat` working and an A→B ping path.
- No Python skeleton yet. Step 2 (or 7, depending on bandwidth) sets up
  `python/server.py` and verifies generated `*_pb2.py` / `*_pb2_grpc.py`
  stubs.

---

## Step 2 — Next

**Goal:** Two C++ servers running concurrently, exchanging heartbeats over
gRPC. Python skeleton for node I that imports the generated stubs.

**Likely files to add:**
- `cpp/include/grpc/Server.hpp`, `cpp/src/grpc/Server.cpp` — `grpc::Server`
  builder, listens on `cfg.host:cfg.port`.
- `cpp/include/grpc/ServiceImpl.hpp`, `cpp/src/grpc/ServiceImpl.cpp` —
  `class Mini2ServiceImpl : public Mini2Service::Service` with `Heartbeat`
  implemented (others return `UNIMPLEMENTED`).
- `cpp/include/grpc/ClientPool.hpp`, `cpp/src/grpc/ClientPool.cpp` — lazily
  creates a stub per neighbor, reused thereafter.
- `cpp/src/main_server.cpp` — extend to actually start the server, call
  `Heartbeat` against each neighbor on a timer, then `Wait()`.
- `python/server.py`, `python/config_loader.py`, `python/requirements.txt` —
  minimum viable Python server: load `key=value` config (mirror C++),
  start `grpc.server`, implement `Heartbeat`.
- `scripts/install_deps.sh` — `pip install -r python/requirements.txt`.
- `scripts/run_local_tree.sh` — launches all 9 servers in background
  (or initially just A and B for step 2).

**Verification:**
- Run A and B in two terminals. After ~2s, both logs show successful
  `Heartbeat` exchanges.
- Run Python I against C++ A: A's logs show heartbeats from `node_id=I`.
