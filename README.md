# Mini 2 — Distributed Chunked Query Engine

CMPE 275, Spring 2026. A 9-process gRPC overlay (A–I) that scatter-gathers
queries over a sharded NYC 311 dataset, returning results to the client in
manually-chunked fetches (no gRPC streaming).

---

## Read These First

If you are picking this up cold (human or LLM), read in this order:

1. **`mini2-chunks.md`** — the original course brief. Source of truth for
   constraints (no streaming, no async-as-stream, no hardcoding, ≥2 machines,
   typed records, etc.).
2. **`mini2_implementation_plan.md`** — the long-form 30-section design (~2200
   lines). Comprehensive reference for proto, lifecycle, chunking algorithm,
   fairness, experiments, report structure.
3. **`IMPLEMENTATION_PLAN.md`** — the *condensed, decided* version. All
   architecture decisions locked in (tree overlay, hash%9 sharding, Python at
   leaf I, key=value config, 2-machine demo, etc.) with a 13-step build order.
4. **`PROGRESS.md`** — running log of which build steps are done and what's
   next. Update this as you complete each step.

---

## Project Layout

```
mini2/
  mini2-chunks.md             # course brief
  mini2_implementation_plan.md # full 30-section design reference
  IMPLEMENTATION_PLAN.md      # condensed locked-in decisions + step order
  PROGRESS.md                 # current state / what's next
  README.md                   # this file

  proto/mini2.proto           # full RPC surface + ServiceRequest message
  config/tree/*.conf          # localhost configs (ports 50051–50059)
  config/tree-lan/*.conf      # 2-machine LAN configs (TODO)
  dataset/311_2020.csv        # full NYC 311 source, 12.7 GB (gitignored)
  data/partitions/*.csv       # per-node shards from partition_data.py (TODO)

  cpp/
    include/<area>/*.hpp      # config, grpc, query, routing, scheduler, …
    src/<area>/*.cpp
    src/main_server.cpp       # mini2_server <conf>
    src/main_client.cpp       # mini2_client (TODO)
    generated/                # protoc output (gitignored)

  python/                     # Python server for node I (TODO)
  scripts/                    # build/run/test/partition helpers
  experiments/metrics_output/ # per-process CSV metrics
  report/                     # final report and figures

  275_project/275-Mini1-main/ # Mini 1 codebase to lift Record + parser from
```

---

## Lifted from Mini 1

The proto's `ServiceRequest` message and (eventually) the C++ data classes
mirror Mini 1's schema. Two files are intended to be lifted **verbatim** with
only OpenMP/SoA strip-down:

- `275_project/275-Mini1-main/src/ServiceRequest.{h,cpp}`
- `275_project/275-Mini1-main/src/CSVParser.{h,cpp}`

The Mini 1 `DataStore.cpp` is a useful reference but will be reimplemented to
expose a row-id–based API for the index layer.

---

## Build

Requires:
- macOS with Homebrew, `brew install grpc protobuf`
- CMake ≥ 3.20
- Clang 17+ (Homebrew LLVM at `/opt/homebrew/opt/llvm`)

```bash
./scripts/build.sh
```

Artifacts land in `build/mini2_server` (and `mini2_client` once it exists).

Override the gRPC/Protobuf prefix if not on `/opt/homebrew`:

```bash
MINI2_PREFIX=/usr/local ./scripts/build.sh
```

---

## Run (current state — step 1 only)

```bash
./build/mini2_server config/tree/A.conf
```

Today this just loads the config, prints node identity + neighbors, confirms
the gRPC stubs link, and exits. Per-step run instructions go into
`PROGRESS.md` as new steps land.

---

## Constraints (Do Not Forget)

From `mini2-chunks.md`. These are graded.

- **No** gRPC streaming.
- **No** gRPC async API used to hide stream control.
- **No** hardcoded node identity, role, host, port, or topology.
- **No** flat hub-and-spoke design (must use the tree overlay's depth).
- **No** shared memory for responses.
- **No** JavaScript/Java/C#, no IDE-VM execution.
- Only **A** is allowed to talk to the external client.
- Each peer owns a **disjoint** subset of the data — no replication or mirroring.
- Final run must use **at least 2 physical machines**.
- Use **typed** records (ints, doubles, enums, bools), not strings-for-everything.

---

## Locked-in Decisions (see IMPLEMENTATION_PLAN.md for full rationale)

| Topic | Choice |
|---|---|
| Scope | Strong (MVP + §29 extras) |
| Overlay | Tree only: A-B, B-C, B-D, B-E, E-F, E-D, E-G, A-H, A-G, A-I |
| Sharding | `hash(unique_key) % 9` |
| Python node | I (leaf) |
| Config format | `key=value` text |
| Concurrency | gRPC sync server + fixed worker thread pool |
| Indexes | borough, complaint_type, created_date, coarse lat/lon grid |
| Final demo | 2 machines — m1: A,B,C,D · m2: E,F,G,H,I |

---

## Conventions for Continuing Development

- **One step at a time.** Each step in `IMPLEMENTATION_PLAN.md` § "Build Order"
  is meant to compile, run, and be smoke-tested before moving on. Update
  `PROGRESS.md` after each.
- **No streaming.** If you find yourself reaching for `stream` in proto or
  `grpc::ClientAsyncReader`, stop — it violates the spec.
- **Configs over flags.** If a value depends on the node, it goes in the
  `key=value` config, not in code or CLI args.
- **Realistic types.** Match Mini 1's `ServiceRequest` field types
  (int64 keys, time_t dates, double coords, enums for borough/status/channel).
- **Metrics on every event.** Per `IMPLEMENTATION_PLAN.md` § Metrics — append a
  CSV row at every interesting transition. Skipping this hurts the report.
