# Mini 2 Implementation Plan: Distributed Chunked Query Engine

## 1. Project Objective

Build a **multi-process distributed query system** using **gRPC** where:

1. A client sends a query only to **Process A**.
2. Process A acts as the public gateway into a private distributed network.
3. A forwards the query through an overlay of processes.
4. Each process owns a subset of the data.
5. Each process searches only its own data.
6. Partial results return toward A.
7. A returns the final result to the client in **manual chunks**, not using gRPC streaming.
8. The system supports fairness, cancellation, memory control, and performance measurement.

The project moves from Mini 1’s internal process parallelization into **multi-process coordination, distributed queries, temporal/spatial decoupling, gRPC messaging, and chunk sizing**.

---

## 2. Required Constraints

### 2.1 Required

You must have:

```text
C++ client
C++ server
Python server
gRPC communication
Separate process shells
Configuration-driven identity, role, host, port, and edges
At least 2 physical computers for final run
A, B, C, D, E, F, G, H, I process network
Manual chunking
A as the only public client-facing server
Each peer owns a subset of the data
No sharing, no replication, no mirroring
```

### 2.2 Forbidden

Do not use:

```text
gRPC streaming API
gRPC async API to hide stream control
hardcoded process identity
hardcoded hostname
hardcoded topology
flat hub-and-spoke design
shared memory for responses
JavaScript / Node
Java
C#
heavy Anaconda dependencies
IDE VM execution
chat-app style fixed-response demo
UI-heavy application focus
```

You can use gRPC, protobuf, C++, Python, shell scripts, CMake, basic Python scripts, and lightweight test utilities.

---

## 3. Recommended Architecture

Use this architecture:

```text
                 External Client
                       |
                       v
                +-------------+
                |  Process A  |
                |  Gateway    |
                +------+------+ 
                       |
        ---------------------------------
        |               |               |
        v               v               v
   Other C++        Other C++       Python Server
   Processes        Processes       Process
```

Process A is special because it is the only process that talks directly to the external client. Every other process only talks to its neighbors.

Each process has the same internal modules:

```text
ConfigManager
GrpcServer
GrpcClientPool
TopologyManager
RequestRouter
RequestTracker
LocalQueryEngine
ChunkManager
Scheduler
ResultCache
MetricsRecorder
Logger
```

---

## 4. Choose the Overlay

Choose one overlay early and stick with it.

### Option A: Tree Overlay

Recommended for this project because it naturally demonstrates forwarding and aggregation.

```text
AB, BC, BD, BE, EF, ED, EG, AH, AG, AI
```

The configured undirected edges are:

```text
A-B
B-C
B-D
B-E
E-F
E-D
E-G
A-H
A-G
A-I
```

Example neighbor configuration:

```yaml
A:
  neighbors: [B, H, G, I]

B:
  neighbors: [A, C, D, E]

C:
  neighbors: [B]

D:
  neighbors: [B, E]

E:
  neighbors: [B, F, D, G]

F:
  neighbors: [E]

G:
  neighbors: [A, E]

H:
  neighbors: [A]

I:
  neighbors: [A]
```

### Option B: 3x3 Grid

```text
A  B  C
D  E  F
G  H  I
```

Edges are horizontal and vertical only:

```text
A-B, B-C
D-E, E-F
G-H, H-I
A-D, B-E, C-F
D-G, E-H, F-I
```

This is also valid, but forwarding logic may be slightly more complex.

---

## 5. Data Ownership Plan

Each process owns a subset of the dataset. Do not replicate the same data everywhere.

A simple ownership model is:

```text
A owns shard 0
B owns shard 1
C owns shard 2
D owns shard 3
E owns shard 4
F owns shard 5
G owns shard 6
H owns shard 7
I owns shard 8
```

Use a deterministic sharding function.

Example:

```cpp
int shard_for_record(const Record& r) {
    return hash(r.key) % 9;
}
```

Then map shard numbers to nodes:

```yaml
shards:
  0: A
  1: B
  2: C
  3: D
  4: E
  5: F
  6: G
  7: H
  8: I
```

During startup, each process loads only records assigned to itself.

### Better data ownership options

Choose one based on your Mini 1 dataset.

#### Option 1: Hash by ID

Good general choice.

```cpp
owner = hash(unique_key) % num_nodes;
```

Pros:

```text
balanced
easy
deterministic
works for many datasets
```

Cons:

```text
range queries must hit all nodes
```

#### Option 2: Range by date

Good if queries are mostly time-based.

```text
A: January
B: February
C: March
...
```

Pros:

```text
date queries can target fewer nodes
```

Cons:

```text
data may be imbalanced
hot months create hotspots
```

#### Option 3: Range by category

Example: complaint type, borough, stock symbol, product type.

Pros:

```text
easy to explain
targeted queries possible
```

Cons:

```text
natural skew can overload a few nodes
```

Recommended: use **hash by primary key** for balance, and broadcast most queries to all nodes. This is simpler and matches distributed scatter-gather.

---

## 6. Project Directory Structure

Use clean organization.

```text
mini2/
  README.md
  CMakeLists.txt

  proto/
    mini2.proto

  config/
    tree/
      A.yaml
      B.yaml
      C.yaml
      D.yaml
      E.yaml
      F.yaml
      G.yaml
      H.yaml
      I.yaml
    grid/
      A.yaml
      ...

  data/
    sample/
      records.csv
    partitions/
      A.csv
      B.csv
      ...

  cpp/
    include/
      config/
        ConfigManager.hpp
      grpc/
        Server.hpp
        ClientPool.hpp
      query/
        Query.hpp
        QueryEngine.hpp
        ChunkManager.hpp
      routing/
        Router.hpp
        RequestTracker.hpp
      scheduler/
        Scheduler.hpp
      metrics/
        Metrics.hpp
      model/
        Record.hpp

    src/
      main_server.cpp
      main_client.cpp
      config/
      grpc/
      query/
      routing/
      scheduler/
      metrics/
      model/

  python/
    server.py
    config_loader.py
    query_engine.py

  scripts/
    build.sh
    partition_data.py
    run_local_tree.sh
    run_local_grid.sh
    run_two_machines_host1.sh
    run_two_machines_host2.sh
    kill_all.sh
    test_smoke.sh
    test_many_clients.sh
    collect_metrics.sh

  experiments/
    chunk_size/
    fairness/
    topology/
    cancellation/
    metrics_output/

  report/
    figures/
    tables/
    mini2_report.md
```

---

## 7. Configuration Design

Do not hardcode node identity, role, host, port, or neighbors.

Each process gets a config file.

Example `config/tree/A.yaml`:

```yaml
node_id: A
role: gateway
language: cpp
host: 192.168.1.10
port: 50051

dataset_path: data/partitions/A.csv

topology:
  overlay: tree
  neighbors:
    - id: B
      host: 192.168.1.10
      port: 50052
    - id: H
      host: 192.168.1.11
      port: 50058
    - id: G
      host: 192.168.1.11
      port: 50057
    - id: I
      host: 192.168.1.11
      port: 50059

chunking:
  default_chunk_records: 500
  max_chunk_bytes: 65536
  adaptive: false

scheduler:
  policy: round_robin
  max_active_requests: 32

timeouts:
  request_ttl_ms: 10000
  peer_timeout_ms: 3000
  client_poll_timeout_ms: 1000

metrics:
  output_path: experiments/metrics_output/A_metrics.csv
```

Example `config/tree/C.yaml`:

```yaml
node_id: C
role: worker
language: cpp
host: 192.168.1.10
port: 50053

dataset_path: data/partitions/C.csv

topology:
  overlay: tree
  neighbors:
    - id: B
      host: 192.168.1.10
      port: 50052

chunking:
  default_chunk_records: 500
  max_chunk_bytes: 65536
  adaptive: false

scheduler:
  policy: round_robin

timeouts:
  request_ttl_ms: 10000
  peer_timeout_ms: 3000

metrics:
  output_path: experiments/metrics_output/C_metrics.csv
```

---

## 8. Protobuf API Design

You need two categories of RPCs:

1. External client-to-A RPCs.
2. Internal peer-to-peer RPCs.

Since you cannot use gRPC streaming, chunking must be done through repeated unary calls.

### 8.1 Recommended `mini2.proto`

```proto
syntax = "proto3";

package mini2;

service Mini2Service {
  // External client calls this on A only.
  rpc SubmitQuery(QueryRequest) returns (QueryAccepted);

  // External client calls this repeatedly on A only.
  rpc FetchChunk(ChunkFetchRequest) returns (ChunkResponse);

  // External client may cancel.
  rpc CancelQuery(CancelRequest) returns (CancelAck);

  // Internal peer request forwarding.
  rpc PeerQuery(PeerQueryRequest) returns (PeerQueryAck);

  // Internal peer sends result chunk back to parent.
  rpc PushPeerChunk(PeerChunk) returns (PeerChunkAck);

  // Health check / heartbeat.
  rpc Heartbeat(HeartbeatRequest) returns (HeartbeatReply);
}

message QueryRequest {
  string client_id = 1;
  string query_text = 2;
  QueryFilter filter = 3;
  int32 requested_chunk_records = 4;
  int64 max_chunk_bytes = 5;
}

message QueryFilter {
  string field_name = 1;
  string op = 2;
  string value = 3;

  // Optional typed values.
  int64 int_value = 4;
  double double_value = 5;
  bool bool_value = 6;

  // Optional range.
  string start = 7;
  string end = 8;
}

message QueryAccepted {
  string request_id = 1;
  bool accepted = 2;
  string message = 3;
}

message ChunkFetchRequest {
  string client_id = 1;
  string request_id = 2;
  int32 max_records = 3;
}

message ChunkResponse {
  string request_id = 1;
  int32 chunk_id = 2;
  repeated Record records = 3;
  bool done = 4;
  bool cancelled = 5;
  string message = 6;
}

message CancelRequest {
  string client_id = 1;
  string request_id = 2;
  string reason = 3;
}

message CancelAck {
  string request_id = 1;
  bool cancelled = 2;
}

message PeerQueryRequest {
  string request_id = 1;
  string origin_node = 2;
  string parent_node = 3;
  string sender_node = 4;
  QueryFilter filter = 5;
  int64 ttl_ms = 6;
  repeated string visited_nodes = 7;
  int32 chunk_records = 8;
  int64 max_chunk_bytes = 9;
}

message PeerQueryAck {
  string request_id = 1;
  string receiver_node = 2;
  bool accepted = 3;
  string message = 4;
}

message PeerChunk {
  string request_id = 1;
  string source_node = 2;
  string destination_node = 3;
  int32 chunk_id = 4;
  repeated Record records = 5;
  bool source_done = 6;
  string message = 7;
}

message PeerChunkAck {
  string request_id = 1;
  string receiver_node = 2;
  bool accepted = 3;
}

message HeartbeatRequest {
  string node_id = 1;
  int64 timestamp_ms = 2;
}

message HeartbeatReply {
  string node_id = 1;
  bool ok = 2;
  int64 timestamp_ms = 3;
}

message Record {
  int64 id = 1;
  string name = 2;
  string category = 3;
  int64 timestamp = 4;
  double latitude = 5;
  double longitude = 6;
  int32 count = 7;
  bool active = 8;
}
```

Adapt `Record` to your actual dataset. The important point is: **do not model everything as strings**.

---

## 9. Core Request Flow

### 9.1 Submit query

```text
Client -> A: SubmitQuery(filter)
A:
  creates request_id
  stores RequestState
  runs local query on A's data
  forwards PeerQuery to neighbors
  returns request_id to client
```

The client does not get results immediately from `SubmitQuery`. It only gets a `request_id`.

### 9.2 Fetch chunks

```text
Client -> A: FetchChunk(request_id)
A:
  checks ResultCache for available records
  returns up to N records
  if no data yet and request still running:
      returns empty chunk with done=false
  if all peers done and cache empty:
      returns done=true
```

This creates manual streaming without gRPC streaming.

### 9.3 Peer forwarding

When a node receives `PeerQuery`:

```text
Worker receives PeerQuery
Worker checks visited_nodes to avoid loops
Worker registers request state
Worker runs local query
Worker chunks local results
Worker sends chunks back to parent using PushPeerChunk
Worker forwards PeerQuery to neighbors except parent/visited nodes
Worker waits for child completion
Worker sends source_done=true to parent
```

### 9.4 Result aggregation

Each node aggregates child results and local results before pushing upward.

Simple design:

```text
leaf nodes push chunks to parent
intermediate nodes push local + child chunks to parent
A receives all chunks and stores them for client polling
```

A does not need to wait for all results before the client can start fetching. That is important for “time to first chunk.”

---

## 10. Request State Model

Create a `RequestState` object.

```cpp
enum class RequestStatus {
    RUNNING,
    DONE,
    CANCELLED,
    FAILED,
    EXPIRED
};

struct RequestState {
    std::string request_id;
    std::string client_id;
    std::string origin_node;
    std::string parent_node;

    QueryFilter filter;

    RequestStatus status;

    std::chrono::steady_clock::time_point created_at;
    std::chrono::steady_clock::time_point deadline;

    std::set<std::string> expected_children;
    std::set<std::string> completed_children;

    std::deque<Record> result_queue;

    int next_chunk_id;
    int chunks_sent_to_client;
    int records_returned_to_client;

    bool local_done;
    bool all_children_done;

    std::mutex mutex;
};
```

### Completion condition

A request is done on a node when:

```text
local query is done
AND all forwarded child requests are done
AND all queued results have been pushed upward or returned to client
```

For A:

```text
done = local_done
       AND all child branches done
       AND result_queue is empty
```

---

## 11. Routing Logic

### 11.1 Avoid loops

Each `PeerQueryRequest` includes:

```text
visited_nodes
parent_node
sender_node
```

When a node receives a request:

```cpp
if (visited_nodes contains my_node_id) {
    reject or ignore;
}
```

Then forward to:

```cpp
for neighbor in neighbors:
    if neighbor != sender_node
    and neighbor not in visited_nodes:
        forward request
```

### 11.2 Parent-child reply routing

Every peer replies only to the node that sent it the request.

Example:

```text
A sends to B
B sends to E
E sends to F
F sends result chunks to E
E sends combined chunks to B
B sends combined chunks to A
A serves client
```

This avoids needing all nodes to know how to directly contact A.

---

## 12. Chunking Design

Chunking is the heart of the project.

### 12.1 Fixed chunking

Start with fixed chunk size:

```text
500 records per chunk
or
64 KB per chunk
```

Implementation:

```cpp
std::vector<Record> make_chunk(std::deque<Record>& queue,
                               int max_records,
                               int64_t max_bytes) {
    std::vector<Record> out;
    int64_t bytes = 0;

    while (!queue.empty() && out.size() < max_records) {
        int64_t record_size = estimate_size(queue.front());
        if (!out.empty() && bytes + record_size > max_bytes) {
            break;
        }

        out.push_back(queue.front());
        queue.pop_front();
        bytes += record_size;
    }

    return out;
}
```

### 12.2 Adaptive chunking

After fixed chunking works, add adaptive mode.

Rules:

```text
If peer/client fetches quickly and queue is large:
    increase chunk size

If peer/client is slow or timeout occurs:
    decrease chunk size

If memory pressure is high:
    decrease chunk size

If many active clients exist:
    decrease per-request chunk size for fairness
```

Example:

```cpp
if (last_send_latency_ms < 20 && queue_size > 5000) {
    chunk_records = std::min(chunk_records * 2, 5000);
} else if (last_send_latency_ms > 200) {
    chunk_records = std::max(chunk_records / 2, 100);
}
```

Do not implement adaptive chunking first. Implement it after the baseline works.

---

## 13. Fairness Design

Fairness means one large query should not monopolize A or the worker nodes.

### 13.1 Minimum fairness mechanism

Use a per-request queue and round-robin scheduler.

```text
Request 1 queue: chunk available
Request 2 queue: chunk available
Request 3 queue: chunk available

Scheduler returns:
R1 chunk
R2 chunk
R3 chunk
R1 chunk
R2 chunk
...
```

### 13.2 Scheduler interface

```cpp
class Scheduler {
public:
    void register_request(const std::string& request_id);
    void mark_ready(const std::string& request_id);
    std::optional<std::string> next_request();
    void complete_request(const std::string& request_id);
};
```

### 13.3 Policies to compare

Implement at least two if possible.

#### Greedy

```text
Return as much as possible for the first available request.
```

Pros:

```text
simple
high throughput for one client
```

Cons:

```text
unfair
large requests can starve small ones
```

#### Round-robin

```text
Each request gets one chunk per scheduling cycle.
```

Pros:

```text
fair
easy to explain
```

Cons:

```text
slightly more overhead
```

#### Weighted round-robin

```text
Small queries get higher priority.
Interactive clients get more frequent chunks.
```

Optional.

---

## 14. Cancellation Design

You need to handle:

```text
client cancels
client disconnects
request expires
client abandons request
```

### 14.1 Client cancellation

```text
Client -> A: CancelQuery(request_id)
A:
  marks request cancelled
  discards queued chunks
  forwards cancellation to children
```

Add internal cancel RPC if needed:

```proto
rpc PeerCancel(PeerCancelRequest) returns (PeerCancelAck);
```

Or reuse `PeerQuery` state and add cancellation later.

### 14.2 Abandoned request

If client stops fetching chunks:

```text
A tracks last_fetch_time
If now - last_fetch_time > abandon_timeout:
    cancel request
    discard cache
    notify children
```

Example:

```yaml
timeouts:
  abandon_timeout_ms: 15000
```

### 14.3 TTL expiration

Every request has deadline:

```text
created_at + request_ttl_ms
```

If deadline passes:

```text
status = EXPIRED
drop queued records
stop local query if possible
ignore future peer chunks for that request
```

---

## 15. Local Query Engine

Your local query engine loads typed records and returns matching records.

### 15.1 Record example

```cpp
struct Record {
    int64_t id;
    std::string name;
    std::string category;
    int64_t timestamp;
    double latitude;
    double longitude;
    int32_t count;
    bool active;
};
```

### 15.2 Query examples

Support at least three query types:

```text
id range
timestamp range
category equals
numeric field greater-than / less-than
geo bounding box, if using location data
```

Example:

```cpp
bool matches(const Record& r, const QueryFilter& f) {
    if (f.field_name == "category" && f.op == "eq") {
        return r.category == f.value;
    }

    if (f.field_name == "timestamp" && f.op == "between") {
        return r.timestamp >= f.start_ts && r.timestamp <= f.end_ts;
    }

    if (f.field_name == "count" && f.op == "gt") {
        return r.count > f.int_value;
    }

    return false;
}
```

### 15.3 Avoid linear-only design if possible

The project says to move away from pure linear searching and adopt better techniques.

Add at least one index.

Recommended indexes:

```cpp
std::unordered_map<std::string, std::vector<int>> category_index;
std::map<int64_t, std::vector<int>> timestamp_index;
std::unordered_map<int64_t, int> id_index;
```

For range queries:

```cpp
auto it_start = timestamp_index.lower_bound(start);
auto it_end = timestamp_index.upper_bound(end);
```

This lets you say in the report:

```text
We compared linear scan vs indexed lookup on each shard.
```

---

## 16. C++ Server Implementation Plan

### Step 1: Build protobuf

Create `proto/mini2.proto`.

Generate C++ files:

```bash
protoc -I proto \
  --grpc_out=cpp/generated \
  --plugin=protoc-gen-grpc=`which grpc_cpp_plugin` \
  proto/mini2.proto

protoc -I proto \
  --cpp_out=cpp/generated \
  proto/mini2.proto
```

### Step 2: Implement config loader

Use YAML if available, or JSON/simple text if you want fewer dependencies.

Simpler custom format example:

```text
node_id=A
role=gateway
host=127.0.0.1
port=50051
dataset_path=data/partitions/A.csv
neighbors=B:127.0.0.1:50052,H:127.0.0.1:50058
chunk_records=500
max_chunk_bytes=65536
```

This avoids needing YAML libraries.

### Step 3: Implement local server

Start with:

```cpp
int main(int argc, char** argv) {
    auto config = ConfigManager::load(argv[1]);

    DataStore store;
    store.load(config.dataset_path);

    QueryEngine engine(store);
    RequestTracker tracker;
    Scheduler scheduler;

    Mini2ServiceImpl service(config, engine, tracker, scheduler);

    RunServer(config.host, config.port, service);
}
```

Run:

```bash
./mini2_server config/tree/A.conf
```

### Step 4: Implement `SubmitQuery`

Only A should accept external `SubmitQuery`.

If a non-A receives `SubmitQuery`, return:

```text
accepted=false
message="External queries must be sent to gateway A"
```

A behavior:

```cpp
QueryAccepted SubmitQuery(QueryRequest req) {
    request_id = generate_uuid();

    tracker.create(request_id, req);

    start_local_query_async(request_id);
    forward_to_neighbors(request_id, req.filter);

    return {request_id, true, "accepted"};
}
```

If you want to avoid actual async complexity, you can use background `std::thread` per request. That is acceptable if managed carefully.

### Step 5: Implement `PeerQuery`

Every server supports this.

```cpp
PeerQueryAck PeerQuery(PeerQueryRequest req) {
    if (already_seen(req.request_id)) return ack;

    tracker.create_peer_request(req);

    start_local_query_async(req.request_id);
    forward_to_unvisited_neighbors(req);

    return ack;
}
```

### Step 6: Implement `PushPeerChunk`

```cpp
PeerChunkAck PushPeerChunk(PeerChunk chunk) {
    auto& state = tracker.get(chunk.request_id);

    if (state.status == CANCELLED || state.status == EXPIRED) {
        return ack(false);
    }

    state.result_queue.push_back(chunk.records);

    if (chunk.source_done) {
        state.completed_children.insert(chunk.source_node);
    }

    return ack(true);
}
```

For A, these records become available to the external client.

For intermediate nodes, these records become available for pushing upward.

### Step 7: Implement internal chunk pusher

Each non-A node should push chunks to its parent.

Simple version:

```cpp
while request not done:
    if result_queue has records:
        chunk = make_chunk()
        PushPeerChunk(parent, chunk)
    else:
        sleep(10ms)

after local + children done:
    PushPeerChunk(parent, source_done=true)
```

### Step 8: Implement `FetchChunk`

Only A should allow this.

```cpp
ChunkResponse FetchChunk(ChunkFetchRequest req) {
    auto& state = tracker.get(req.request_id);

    if (state.cancelled) return cancelled response;

    auto records = make_chunk(state.result_queue, req.max_records, config.max_chunk_bytes);

    bool done = state.local_done &&
                state.all_children_done &&
                state.result_queue.empty();

    return ChunkResponse {
        request_id,
        chunk_id,
        records,
        done
    };
}
```

---

## 17. Python Server Plan

The Python server must implement the same protobuf service.

Use Python for one or more worker nodes, for example process `I`.

Run:

```bash
python3 python/server.py config/tree/I.conf
```

Python server responsibilities:

```text
load config
load local shard
accept PeerQuery
run local query
push chunks to parent using gRPC client stubs
respond to Heartbeat
```

Do not make Python the gateway unless you want extra complexity. Use C++ for A.

Python server pseudocode:

```python
class Mini2Service(mini2_pb2_grpc.Mini2ServiceServicer):
    def PeerQuery(self, request, context):
        request_id = request.request_id
        tracker.create(request_id, request)

        threading.Thread(
            target=self.process_peer_query,
            args=(request,),
            daemon=True
        ).start()

        return mini2_pb2.PeerQueryAck(
            request_id=request_id,
            receiver_node=self.node_id,
            accepted=True
        )

    def process_peer_query(self, request):
        results = self.query_engine.search(request.filter)

        for chunk_id, chunk in enumerate(make_chunks(results)):
            peer_chunk = mini2_pb2.PeerChunk(
                request_id=request.request_id,
                source_node=self.node_id,
                destination_node=request.parent_node,
                chunk_id=chunk_id,
                records=chunk,
                source_done=False
            )
            self.parent_stub.PushPeerChunk(peer_chunk)

        done_chunk = mini2_pb2.PeerChunk(
            request_id=request.request_id,
            source_node=self.node_id,
            destination_node=request.parent_node,
            source_done=True
        )
        self.parent_stub.PushPeerChunk(done_chunk)
```

---

## 18. Client Plan

The C++ client should support:

```bash
./mini2_client --host 127.0.0.1 --port 50051 \
  --field category --op eq --value noise \
  --chunk-records 500
```

Flow:

```cpp
auto accepted = stub.SubmitQuery(query);

while (!done) {
    auto chunk = stub.FetchChunk(request_id);

    print chunk summary;
    save chunk records if needed;

    if (chunk.done) break;

    sleep or immediately fetch next chunk;
}
```

Add cancellation test:

```bash
./mini2_client --cancel-after 3
```

This fetches three chunks, then sends `CancelQuery`.

---

## 19. Testing Plan

### 19.1 Unit tests

Test these without gRPC first:

```text
ConfigManager loads node ID, host, port, neighbors
QueryEngine returns expected records
ChunkManager respects max records
ChunkManager respects max bytes
Scheduler returns requests round-robin
RequestTracker marks children complete correctly
```

### 19.2 Local smoke test

Run all 9 processes on localhost.

```bash
scripts/run_local_tree.sh
```

Expected:

```text
A starts on 50051
B starts on 50052
...
I starts on 50059
Client submits query to A
A forwards to B,H,G,I
B forwards to C,D,E
E forwards to F,D,G as appropriate without loops
Chunks return to A
Client fetches chunks until done
```

### 19.3 Two-machine test

Example split:

Machine 1:

```text
A, B, C, D
```

Machine 2:

```text
E, F, G, H, I
```

Or:

Machine 1:

```text
A, B, D, H
```

Machine 2:

```text
C, E, F, G, I
```

Use actual LAN IP addresses in config.

### 19.4 Python server test

Run one node as Python, preferably leaf node first.

Recommended:

```text
I = Python server
```

Once that works, try:

```text
F = Python server
```

Do not make a high-traffic intermediate node Python unless you have time to debug.

### 19.5 Cancellation test

```text
Client submits large query
Client fetches 2 chunks
Client cancels
A marks request cancelled
A discards cache
A sends cancellation to children or ignores later chunks
Peers stop or finish but chunks are ignored
Metrics record cancellation
```

### 19.6 Fairness test

Run two clients:

```text
Client 1: huge query
Client 2: small query
```

Measure:

```text
time to first chunk for each
time to completion for small query
whether small query waits behind huge query
```

Compare:

```text
greedy scheduler
round-robin scheduler
```

---

## 20. Experiments to Include in Report

You should not only build the system. You need measurements.

### Experiment 1: Chunk size impact

Test chunk sizes:

```text
100 records
500 records
1000 records
5000 records
```

Measure:

```text
total query time
time to first chunk
number of chunks
average chunk latency
memory used at A
network bytes
```

Expected discussion:

```text
Small chunks improve responsiveness and fairness but increase message overhead.
Large chunks improve throughput but increase memory pressure and delay first response.
```

### Experiment 2: Fairness

Compare:

```text
greedy
round-robin
```

Run:

```text
1 large query
1 small query
3 simultaneous medium queries
```

Measure:

```text
small query completion time
large query completion time
p95 chunk wait time
queue depth
```

Expected discussion:

```text
Round-robin may reduce peak throughput slightly but prevents starvation.
```

### Experiment 3: Topology impact

If time permits, compare tree vs grid.

Measure:

```text
message count
end-to-end latency
time to first chunk
completion time
duplicate/loop prevention overhead
```

Expected discussion:

```text
Tree is easier for aggregation.
Grid gives more routing paths but increases loop-control complexity.
```

### Experiment 4: Local vs distributed

Run:

```text
all nodes localhost
two physical machines
three physical machines if possible
```

Measure:

```text
local query latency
remote query latency
network overhead
```

Expected discussion:

```text
Remote distribution introduces latency but improves capacity and parallelism when data is large.
```

### Experiment 5: Linear scan vs indexed lookup

On each shard:

```text
linear scan query
indexed category query
indexed timestamp/range query
```

Measure:

```text
local query time
memory overhead of index
index build time
```

Expected discussion:

```text
Indexes improve repeated query speed but cost memory and startup time.
```

---

## 21. Metrics to Record

Create a CSV metrics file per process.

Example columns:

```csv
timestamp_ms,node_id,request_id,event,records,bytes,queue_depth,latency_ms,active_requests
```

Events:

```text
submit_query
peer_query_received
peer_query_forwarded
local_query_start
local_query_done
chunk_created
chunk_pushed
chunk_received
client_chunk_fetched
request_done
request_cancelled
request_expired
```

Example:

```csv
1710000000000,A,req-123,submit_query,0,0,0,0,1
1710000000025,B,req-123,local_query_done,1200,0,0,25,1
1710000000030,B,req-123,chunk_pushed,500,65000,700,5,1
```

---

## 22. Milestone Schedule

### Milestone 1: Basecamp

Goal:

```text
All processes start from config.
Each server knows its node ID and neighbors.
C++ client can ping A.
A can ping neighbors.
Python server can start and respond to heartbeat.
```

Deliverables:

```text
Config files
C++ server boot
Python server boot
Heartbeat RPC
Local run script
```

### Milestone 2: Local query engine

Goal:

```text
Each process loads its own shard.
QueryEngine can run local typed queries.
ChunkManager can split local results.
```

Deliverables:

```text
Record struct
CSV loader
Partition script
QueryEngine
ChunkManager unit test
```

### Milestone 3: A-only client query

Goal:

```text
Client submits query to A.
A searches only local data.
Client fetches chunks from A.
No peer forwarding yet.
```

Deliverables:

```text
SubmitQuery
FetchChunk
CancelQuery basic
RequestTracker
ResultCache
```

### Milestone 4: Peer forwarding

Goal:

```text
A forwards query to neighbors.
Neighbors run local search.
Neighbors push chunks back to A.
Client receives combined chunks.
```

Deliverables:

```text
PeerQuery
PushPeerChunk
GrpcClientPool
basic forwarding
visited node tracking
```

### Milestone 5: Multi-hop forwarding

Goal:

```text
Tree/grid overlay works.
Intermediate nodes forward requests.
Intermediate nodes aggregate child results.
No loops.
```

Deliverables:

```text
parent/child request tracking
visited_nodes
completion detection
multi-hop test
```

### Milestone 6: Fairness and cancellation

Goal:

```text
Multiple clients can query at once.
Round-robin chunk scheduling works.
Cancellation cleans request state.
Abandoned requests expire.
```

Deliverables:

```text
Scheduler
CancelQuery
TTL cleanup thread
fairness test script
```

### Milestone 7: Experiments and report

Goal:

```text
Collect charts and tables.
Run final two-machine demo.
Prepare report and presentation.
```

Deliverables:

```text
metrics CSVs
experiment scripts
plots
final report
demo checklist
```

---

## 23. Demo Plan

Your live demo should be simple and convincing.

### Demo sequence

1. Show config files proving no hardcoding.
2. Start servers in separate terminals.
3. Show A as gateway.
4. Show at least one Python server running.
5. Run client query.
6. Show A forwarding to peers.
7. Show chunks returning.
8. Show client fetching chunks.
9. Run two clients to show fairness.
10. Cancel one request.
11. Show metrics output.

### Example demo commands

Terminal 1:

```bash
./mini2_server config/tree/A.conf
```

Terminal 2:

```bash
./mini2_server config/tree/B.conf
```

Terminal 3:

```bash
python3 python/server.py config/tree/I.conf
```

Client:

```bash
./mini2_client --gateway 192.168.1.10:50051 \
  --field category --op eq --value NOISE \
  --chunk-records 500
```

Cancellation:

```bash
./mini2_client --gateway 192.168.1.10:50051 \
  --field category --op eq --value NOISE \
  --chunk-records 500 \
  --cancel-after 3
```

Fairness:

```bash
scripts/test_many_clients.sh
```

---

## 24. Report Structure

Use this structure.

```text
1. Abstract
2. Problem Statement
3. System Requirements and Constraints
4. Architecture
5. Overlay Design
6. Data Partitioning Strategy
7. Request Lifecycle
8. Manual Chunking Design
9. Fairness Scheduler
10. Cancellation and TTL Handling
11. C++/Python gRPC Implementation
12. Experiments
13. Results
14. Analysis and Tradeoffs
15. Failures / Bugs / Limitations
16. Future Work
17. Conclusion
```

### Must-answer questions in the report

Answer these clearly:

```text
How did you verify Basecamp?
How are nodes configured?
Why did you choose tree or grid?
How are loops prevented?
How does A match requests to replies?
How do chunks reduce memory pressure?
How is fairness implemented?
What happens when the client cancels?
What happens when the client disappears?
What data structures are typed and realistic?
What did you measure?
What changed performance most?
What would you improve with more time?
```

---

## 25. Team Role Split

For a 4-person team:

| Role | Responsibilities |
|---|---|
| Member 1: gRPC / Basecamp Lead | Proto file, C++ server, C++ client, Python server, build system |
| Member 2: Routing / Overlay Lead | Config, topology, forwarding, visited nodes, parent-child completion |
| Member 3: Query / Chunking Lead | Dataset partitioning, typed records, local indexes, chunk manager |
| Member 4: Fairness / Metrics / Report Lead | Scheduler, cancellation, TTL, metrics, experiments, plots, final report |

For a 3-person team:

| Role | Responsibilities |
|---|---|
| Member 1 | gRPC, C++ server/client, Python server |
| Member 2 | Routing, topology, request tracker, cancellation |
| Member 3 | Query engine, chunking, fairness, metrics/report |

---

## 26. Implementation Priority

Build in this exact order:

```text
1. Protobuf compiles
2. One C++ server starts
3. C++ client calls SubmitQuery on A
4. A returns request_id
5. A local QueryEngine works
6. A FetchChunk works
7. B receives PeerQuery from A
8. B pushes PeerChunk to A
9. Multi-hop forwarding works
10. Python worker works
11. Multiple clients work
12. Cancellation works
13. Round-robin fairness works
14. Metrics collection works
15. Experiments and report
```

Do not start with adaptive chunking, advanced indexing, or failure recovery. Those are valuable but should come after the basic distributed flow works.

---

## 27. Common Failure Modes to Avoid

### Mistake 1: Building a flat system

Bad:

```text
A sends directly to B,C,D,E,F,G,H,I only
```

Why bad:

```text
It avoids the overlay forwarding challenge.
```

Use tree/grid forwarding.

### Mistake 2: Returning one huge response

Bad:

```text
SubmitQuery returns all results.
```

Why bad:

```text
The project is about delayed transfer, chunking, memory, bandwidth, and fairness.
```

### Mistake 3: Using gRPC streaming

Bad:

```proto
rpc Query(QueryRequest) returns (stream ChunkResponse);
```

Why bad:

```text
The project says not to use gRPC streaming because stream-control should be your algorithm.
```

### Mistake 4: Hardcoding node identity

Bad:

```cpp
std::string node_id = "A";
```

Good:

```cpp
std::string node_id = config.node_id;
```

### Mistake 5: Using strings for everything

Bad:

```cpp
struct Record {
    std::vector<std::string> fields;
};
```

Good:

```cpp
struct Record {
    int64_t id;
    std::string category;
    int64_t timestamp;
    double latitude;
    double longitude;
    bool active;
};
```

### Mistake 6: No metrics

A working system without measurements will look weak. Metrics are essential.

---

## 28. Minimum Viable Final Version

If time is short, deliver this:

```text
C++ A gateway
C++ B/C/D workers
Python I worker
Tree overlay
Config-driven nodes
A-only external client
PeerQuery forwarding
PushPeerChunk return path
Manual FetchChunk loop
Fixed chunk size
Round-robin fairness
Cancellation on A
Metrics CSV
Two-machine final run
```

This is enough for a solid project.

---

## 29. Strong Final Version

If you have more time, add:

```text
indexed local queries
adaptive chunk sizing
abandoned-request cleanup
tree vs grid comparison
Python intermediate node
heartbeat status
failure experiment
weighted fairness
memory usage tracking
plots from metrics
```

---

## 30. Final Summary

Your project should demonstrate that your team can build a real distributed backend system:

```text
configured overlay
distributed data ownership
multi-hop gRPC communication
manual chunked result transfer
request tracking
fairness between clients
typed data structures
C++ and Python interoperability
measured performance tradeoffs
```

The strongest version is not the one with the fanciest UI. The strongest version is the one that clearly shows:

```text
How data is distributed
How requests move
How replies are matched
How chunks are controlled
How fairness is enforced
How memory and bandwidth are conserved
How performance was measured
```
