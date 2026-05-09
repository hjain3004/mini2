# Mini 2: Distributed Chunked Query Engine — Implementation Plan

## Context

We're starting a greenfield build at `/Users/himanshu_jain/275/mini2/` (currently
contains only `mini2_implementation_plan.md` and `mini2-chunks.md`). The project
extends the user's Mini 1 NYC-311 codebase
(`/Users/himanshu_jain/275/mini2/275_project/275-Mini1-main/`) into a
multi-process distributed query engine over gRPC, satisfying the CMPE 275 Mini 2
spec (`mini2-chunks.md`).

The user has chosen the **Strong** target (§29 of the plan): MVP plus indexed
queries, adaptive chunking, abandoned-request cleanup, heartbeat, and full
metrics/plots. The dataset is **NYC 311 Service Requests** (Mini 1 carryover);
the typed `ServiceRequest` class from Mini 1 is the on-the-wire `Record`.

The deliverable is a 9-process tree overlay (A–I) running on **2 LAN machines**,
with a C++ gateway, C++ workers, one Python worker (node I), a C++ client,
fixed+adaptive manual chunking, round-robin fairness, cancellation, and a
metrics-driven report.

## Decisions Locked In

| Topic | Choice |
|---|---|
| Scope | Strong (MVP + §29 extras) |
| Dataset | NYC 311 Service Requests (Mini 1 schema) |
| Overlay | Tree only: AB, BC, BD, BE, EF, ED, EG, AH, AG, AI |
| Mini 1 reuse | Lift `ServiceRequest`, `CSVParser`, AoS DataStore; rewrite rest |
| Final demo | 2 machines — m1: A,B,C,D · m2: E,F,G,H,I |
| Config format | Simple `key=value` text (no YAML/JSON deps) |
| Sharding | `hash(unique_key) % 9` |
| Python node | I (leaf, only neighbor = A) |
| Concurrency | gRPC sync server + fixed worker thread pool |
| Indexes | Borough, complaint_type, created_date (`std::map`), coarse lat/lon grid |
| Scripts | Standard set (build, partition, run_local_tree, run_two_machines_host{1,2}, kill_all, test_smoke) |

## Architecture

```
                       External C++ Client
                                |  (SubmitQuery, FetchChunk, CancelQuery)
                                v
                          +-----------+
                          | A gateway |  m1
                          +-----+-----+
                  ________/  |     \________
                 /           |              \
               B (m1)      H (m2)         G (m2)        I (m2, Python)
              / | \           |              \________/
             C  D  E (m2)
                   / | \
                  F  D  G   (D and G already in tree above; tree edges, not duplicates)
```

Edges (undirected): A-B, B-C, B-D, B-E, E-F, E-D, E-G, A-H, A-G, A-I.
Note **E-D** and **A-G/E-G** create cycles in the underlying graph, so peer
forwarding MUST use `visited_nodes` to keep traversal acyclic; the *logical*
spanning tree from A is computed by each node from its neighbor list using
"don't reply to anyone but the request's `parent_node`."

Per-process modules (same in C++ and Python):

```
ConfigManager          loads node_id, host, port, neighbors, dataset_path, tunables
GrpcServer             grpc::Server + Mini2ServiceImpl (sync, no streaming)
GrpcClientPool         one stub per neighbor, lazy-connect, reconnect on failure
DataStore              AoS vector<ServiceRequest>, owns shard records
IndexSet               Borough/complaint_type/date/geo indexes over DataStore
LocalQueryEngine       matches QueryFilter against IndexSet, falls back to scan
RequestTracker         map<request_id, RequestState> with mutex
Scheduler              round-robin across active requests; dispatches chunk work
ChunkManager           builds chunks honoring max_records + max_bytes
ResultQueue            per-request deque<Record> protected by mutex+cv
MetricsRecorder        appends rows to per-process CSV
Logger                 thin wrapper, level-gated
WorkerPool             fixed std::thread pool (configurable, default 8)
```

Python server mirrors these as small modules under `python/`.

## Directory Layout

```
mini2/
  README.md
  CMakeLists.txt
  proto/mini2.proto
  config/tree/{A..I}.conf            # local-dev (127.0.0.1)
  config/tree-lan/{A..I}.conf        # 2-machine demo (real IPs)
  dataset/311_2020.csv               # full NYC 311 source (12.7 GB), already present
  data/
    partitions/{A..I}.csv            # produced by partition_data.py (streamed from dataset/)
  cpp/
    include/{config,grpc,query,routing,scheduler,metrics,model,chunk}/*.hpp
    src/
      main_server.cpp                # binary: mini2_server <conf>
      main_client.cpp                # binary: mini2_client
      config/ConfigManager.cpp
      grpc/{Server,ClientPool,ServiceImpl}.cpp
      query/{LocalQueryEngine,IndexSet,DataStore}.cpp
      routing/{Router,RequestTracker}.cpp
      scheduler/{Scheduler,WorkerPool}.cpp
      chunk/ChunkManager.cpp
      metrics/MetricsRecorder.cpp
      model/{ServiceRequest,CSVParser}.cpp     # lifted from Mini 1
    generated/                        # protoc output (gitignored)
  python/
    server.py
    config_loader.py
    query_engine.py
    chunk_manager.py
    requirements.txt                  # grpcio, grpcio-tools, protobuf
  scripts/
    build.sh
    partition_data.py
    run_local_tree.sh
    run_two_machines_host1.sh
    run_two_machines_host2.sh
    kill_all.sh
    test_smoke.sh
    test_many_clients.sh
    collect_metrics.sh
  experiments/{chunk_size,fairness,cancellation,linear_vs_indexed,local_vs_distributed}/
  experiments/metrics_output/
  report/{figures,tables,mini2_report.md}
```

## Protobuf (`proto/mini2.proto`)

Adopt §8.1 of the plan verbatim with one tweak: replace the placeholder `Record`
message with a real **`ServiceRequest`** message mirroring the Mini 1 class:

```proto
message ServiceRequest {
  int64  unique_key = 1;
  int64  created_date = 2;          // epoch seconds
  int64  closed_date = 3;
  int64  due_date = 4;
  int64  resolution_updated_date = 5;
  double latitude = 6;
  double longitude = 7;
  int32  incident_zip = 8;
  int32  x_coordinate = 9;
  int32  y_coordinate = 10;
  uint32 borough = 11;              // enum cast
  uint32 status = 12;
  uint32 channel_type = 13;
  string agency = 14;
  string complaint_type = 15;
  string descriptor = 16;
  string incident_address = 17;
  string city = 18;
  // … (remaining string fields kept; trim heavy ones for chunk-byte budget)
}
```

RPCs (sync, unary only — **no streaming**):

```
SubmitQuery       client → A only          returns request_id
FetchChunk        client → A only          returns ChunkResponse with up to N records
CancelQuery       client → A only
PeerQuery         peer → peer              forward filter with visited_nodes
PushPeerChunk     peer → parent peer       deliver child results upward
PeerCancel        peer → peer              propagate cancellation toward leaves
Heartbeat         peer ↔ peer              health check
```

Non-A nodes receiving `SubmitQuery` reply `accepted=false, message="gateway-only"`.

## Request Lifecycle

1. **Submit** — client → A: `SubmitQuery(filter)`. A creates `request_id` (UUID),
   inserts `RequestState{status=RUNNING}` into `RequestTracker`, dispatches the
   local query to `WorkerPool`, and forwards `PeerQuery` to each neighbor with
   `parent_node=A, visited_nodes={A}`. Returns `request_id` immediately.
2. **Peer ingest** — non-A node N receives `PeerQuery`. If `N ∈ visited_nodes`,
   ack and ignore. Else: register state, dispatch local query, forward
   `PeerQuery` to every neighbor not in `visited_nodes ∪ {sender}`,
   appending N to `visited_nodes`.
3. **Local query** — `LocalQueryEngine.run(filter)` uses `IndexSet` for
   indexed predicates (eq/range on borough, complaint_type, created_date, geo
   bbox) and falls back to linear scan for everything else. Streams matches
   into the request's `ResultQueue`.
4. **Upward push** — a per-request "chunk pusher" task on each non-A node
   takes records from the local `ResultQueue`, builds a chunk via
   `ChunkManager`, and calls `PushPeerChunk(parent)`. A leaf marks
   `source_done=true` after its local query finishes. An intermediate node
   pushes its own + children's records, then sends `source_done=true` only
   after its local query is done **and** every expected child has reported
   `source_done`.
5. **Client fetch** — client polls A: `FetchChunk(request_id, max_records)`.
   A returns up to `max_records` from its merged queue. `done=true` only when
   A's local query is done AND all of A's children are done AND the queue is
   drained.
6. **Cancel** — client → A: `CancelQuery`. A flips state to `CANCELLED`, drops
   the queue, and fans out `PeerCancel` toward children. Each node, on
   `PeerCancel`, marks state `CANCELLED`, drops its queue, and forwards to its
   downstream neighbors.
7. **Abandoned** — A's tracker thread checks `now - last_fetch_time >
   abandon_timeout_ms`; if so, behaves as cancellation.
8. **TTL expire** — `created_at + request_ttl_ms` reaped by a janitor thread;
   sets `EXPIRED`, drops queue, ignores future chunks.

### Completion bookkeeping

`RequestState` holds:
- `expected_children` (computed at forward time = neighbors actually forwarded to)
- `completed_children` (filled as each child sends `source_done`)
- `local_done`, `all_children_done = (expected == completed)`

A node is "done" when `local_done && all_children_done && result_queue.empty()`.

## Chunking

**Phase A (MVP)** — fixed: `default_chunk_records=500`, `max_chunk_bytes=65536`.
`ChunkManager::make_chunk(queue, max_records, max_bytes)` pops records until
either limit is hit, returning at least one record (oversize singletons allowed).

**Phase B (Strong target)** — adaptive: per-request `chunk_records` field,
adjusted on each push:

```
if last_send_latency_ms < 20 && queue_size > 5000:
    chunk_records = min(chunk_records * 2, 5000)
elif last_send_latency_ms > 200:
    chunk_records = max(chunk_records / 2, 100)
```

Toggle via `chunking.adaptive=true` in config.

## Fairness

Per-process `Scheduler` keeps a round-robin deque of `request_ids` with
"chunk ready" status. `WorkerPool` threads pull from the scheduler, each
producing exactly one chunk-push (or one client fetch satisfaction) before
re-queueing. Two policies, switchable via `scheduler.policy`:

- `greedy` — drain a single request fully before serving the next
- `round_robin` (default) — one chunk per request per cycle

This gives a clean A/B for §20.2.

## Cancellation & TTL

Janitor thread runs every 500ms on each node:
- For each request: check `deadline` and `last_activity` for abandon timeout.
- Drops queues and emits `PeerCancel` downstream where applicable.
- Records `request_cancelled` / `request_expired` in metrics.

## Local Query Engine + Indexes

Indexes built once at server boot (after CSV partition load):

```cpp
std::unordered_map<Borough,    std::vector<uint32_t>> borough_idx;
std::unordered_map<std::string,std::vector<uint32_t>> complaint_idx;
std::map<time_t,               std::vector<uint32_t>> created_idx;   // ordered
struct GeoCell { int lat_bucket; int lon_bucket; };
std::unordered_map<uint64_t,   std::vector<uint32_t>> geo_idx;       // 0.01° grid
```

`LocalQueryEngine::run(filter)`:
- Picks the most selective indexed predicate, intersects candidate row-id sets,
  applies remaining predicates as a filter pass.
- Falls back to linear scan when no predicate is indexed (used for the
  linear-vs-indexed experiment).

## Metrics

Per-process CSV at `experiments/metrics_output/{node_id}_metrics.csv`:

```
timestamp_ms,node_id,request_id,event,records,bytes,queue_depth,latency_ms,active_requests
```

Events: `submit_query`, `peer_query_received`, `peer_query_forwarded`,
`local_query_start`, `local_query_done`, `chunk_created`, `chunk_pushed`,
`chunk_received`, `client_chunk_fetched`, `request_done`, `request_cancelled`,
`request_expired`, `heartbeat_sent`, `heartbeat_received`.

`scripts/collect_metrics.sh` rsyncs CSVs from both machines into
`experiments/metrics_output/` and `report/` plotting scripts (Python +
matplotlib, lightweight) generate the figures.

## Files to Create

Critical new files (greenfield, so all are new):

- `mini2/proto/mini2.proto`
- `mini2/CMakeLists.txt` — top-level, finds `Protobuf` and `gRPC`, generates
  stubs into `cpp/generated/`, builds `mini2_server` and `mini2_client`.
- `mini2/cpp/include/config/ConfigManager.hpp` + `src/config/ConfigManager.cpp`
- `mini2/cpp/include/grpc/{Server,ClientPool,ServiceImpl}.hpp` + sources
- `mini2/cpp/include/query/{DataStore,IndexSet,LocalQueryEngine}.hpp` + sources
- `mini2/cpp/include/routing/{Router,RequestTracker,RequestState}.hpp` + sources
- `mini2/cpp/include/scheduler/{Scheduler,WorkerPool}.hpp` + sources
- `mini2/cpp/include/chunk/ChunkManager.hpp` + source
- `mini2/cpp/include/metrics/MetricsRecorder.hpp` + source
- `mini2/cpp/include/model/{ServiceRequest,CSVParser}.hpp` + sources (**lifted from
  `275_project/275-Mini1-main/src/`** — keep enums and primitive-typed fields;
  drop the SoA/OpenMP harness)
- `mini2/cpp/src/main_server.cpp`, `main_client.cpp`
- `mini2/python/{server,config_loader,query_engine,chunk_manager}.py`
- `mini2/config/tree/{A..I}.conf` (localhost, distinct ports 50051–50059)
- `mini2/config/tree-lan/{A..I}.conf` (2-machine IPs)
- `mini2/scripts/{build.sh,partition_data.py,run_local_tree.sh,
  run_two_machines_host1.sh,run_two_machines_host2.sh,kill_all.sh,
  test_smoke.sh,test_many_clients.sh,collect_metrics.sh}`

Files to **lift verbatim** from Mini 1 (with light edits to remove OpenMP/SoA
deps): `ServiceRequest.{h,cpp}`, `CSVParser.{h,cpp}`. The Mini 1 `DataStore.cpp`
is a useful reference but will be reimplemented to expose the row-id–based API
that `IndexSet` needs.

## Build Order (Implementation Sequence)

1. **Skeleton + proto** — CMake builds; `protoc` generates C++ + Python stubs;
   empty `mini2_server` boots from a config and prints `node_id`.
2. **ConfigManager + Heartbeat** — load `key=value` config; A pings B over
   gRPC. Same in Python for I. ✅ Basecamp verified.
3. **DataStore + CSVParser + IndexSet** — load shard CSV, build all four
   indexes, expose `LocalQueryEngine.run(filter)`.
4. **A-only flow** — `SubmitQuery` + `FetchChunk` + `CancelQuery` on A's
   *local* shard; client polls until `done`.
5. **PeerQuery + PushPeerChunk** — single hop A↔B working; visited_nodes
   plumbing in place.
6. **Multi-hop forwarding** — full tree works locally with all 9 servers;
   verify completion semantics under E-D / A-G cycles.
7. **Python node I** — bring up I as a Python leaf; client query reaches it
   through A.
8. **Scheduler + WorkerPool** — round-robin policy; `test_many_clients.sh`
   shows fairness vs greedy.
9. **Cancellation + TTL janitor** — `PeerCancel` propagation; abandoned-fetch
   cleanup.
10. **Adaptive chunking** — flag-gated; tune via experiments.
11. **Metrics + experiments** — run §20.{1,2,4,5} (chunk size, fairness,
    local-vs-distributed, linear-vs-indexed); plot.
12. **Two-machine demo** — finalize `tree-lan/` configs, run §22 demo.
13. **Report** — `report/mini2_report.md` per §24 structure.

## Verification

- **Unit-ish smoke** (`scripts/test_smoke.sh`): boots all 9 local servers,
  runs `mini2_client --field complaint_type --op eq --value "Noise - Street/Sidewalk"`,
  asserts `done=true` and non-zero records returned.
- **Cancellation test**: `mini2_client --cancel-after 3` on a large query;
  inspect `*_metrics.csv` for `request_cancelled` events on A and downstream nodes.
- **Fairness test**: `scripts/test_many_clients.sh` runs 1 huge + 1 small +
  3 medium queries simultaneously under both `greedy` and `round_robin`;
  diff completion times.
- **Indexed-vs-linear**: `mini2_client --use-index off` flag (forces linear
  scan in QueryEngine) vs default; compare `local_query_done` latencies.
- **Two-machine demo**: launch host1 + host2 scripts; client on either
  machine submits to A's LAN IP; verify chunks flow across the LAN per
  metrics.

## Open Items / Risks

- **gRPC + Protobuf install on macOS** — the user's Mini 1 build used Homebrew
  Clang 17. We'll add a `scripts/install_deps.sh` documenting the
  `brew install grpc protobuf` (or vcpkg) path. Worth a 30-min spike before
  step 1.
- **Dataset** — full source: `mini2/dataset/311_2020.csv` (12.7 GB).
  `scripts/partition_data.py` streams it line-by-line (no full in-memory
  load), hashes `unique_key`, and appends to nine output CSVs in
  `mini2/data/partitions/{A..I}.csv` (~1.4 GB each). Each server loads only
  its shard at boot. No sampling — final demo and experiments use the
  full dataset.
- **macOS firewall** — two-machine LAN demo may need an explicit firewall
  allow for ports 50051–50059. Document in README.

## Out of Scope (Now)

- Failure recovery (server crash mid-request) — mentioned in the spec as
  optional; deferred unless the §29 scope finishes early.
- Weighted/priority scheduling — only `greedy` and `round_robin` are
  implemented.
- 3x3 grid overlay — not building; tree-only commitment.
- Web UI / dashboards — explicitly forbidden by the spec.
