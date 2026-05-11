# Mini 2 — Complete Demo Guide

> **Audience**: Beginner-friendly. Every step is explained in detail.
> **Last updated**: 2026-05-09

---

## Table of Contents

1. [Overview — What Are We Demoing?](#1-overview--what-are-we-demoing)
2. [Prerequisites — What You Need Before Starting](#2-prerequisites--what-you-need-before-starting)
3. [Demo Option A — Single Machine (Localhost)](#3-demo-option-a--single-machine-localhost)
4. [Demo Option B — Two Machines (LAN)](#4-demo-option-b--two-machines-lan)
5. [Demo Scenarios — What to Show](#5-demo-scenarios--what-to-show)
6. [Troubleshooting — Common Errors & Solutions](#6-troubleshooting--common-errors--solutions)
7. [Expected Results Reference](#7-expected-results-reference)
8. [Quick-Reference Commands](#8-quick-reference-commands)

---

## 1. Overview — What Are We Demoing?

We have a **distributed query engine** that processes NYC 311 Service Request data across 9 processes (nodes A–I) organized in a tree overlay. Here's what it does:

```
                    External Client
                         |
                    +---------+
                    | A (GW)  |  ← Gateway: only node clients talk to
                    +----+----+
               _____|____|____
              |      |       |        |
              B      H       G        I (Python)
            / | \
           C  D  E
              |  / \
              D  F  G  (cycles handled by visited_nodes)
```

**Key features to demonstrate:**
- ✅ 9-process distributed query (8 C++ + 1 Python)
- ✅ Chunked result streaming (client fetches piece by piece)
- ✅ Round-Robin vs Greedy fairness scheduling
- ✅ Query cancellation propagating through the tree
- ✅ Adaptive chunking (auto-tunes chunk size based on latency)
- ✅ Indexed vs linear scan comparison

**Node assignment for 2-machine demo:**

| Machine 1 — m1 (your MacBook) | Machine 2 — m2 (teammate's MacBook) |
|---|---|
| A (gateway), B, C, D | E, F, G, H, I (Python) |

**Networking:** Both MacBooks connect to the **same WiFi router**. No ethernet needed.

---

## 2. Prerequisites — What You Need Before Starting

### 2.1 Software Requirements

Run these on **both MacBooks**:

```bash
# Install C++ toolchain + gRPC
brew install grpc protobuf cmake python3

# Python dependencies
pip3 install --break-system-packages grpcio grpcio-tools protobuf matplotlib
```

> [!TIP]
> Your teammate can also run `bash scripts/check_deps.sh` to auto-check and install everything.

### 2.2 Verify gRPC is installed

Run on **both MacBooks**:

```bash
# Should print version, e.g., "libprotoc 34.x"
protoc --version

# Verify gRPC C++ plugin exists:
ls /opt/homebrew/bin/grpc_cpp_plugin && echo "gRPC OK"
```

> [!NOTE]
> `pkg-config` is **not** installed by default on macOS and is not needed. If `protoc --version` works and `grpc_cpp_plugin` exists, you're good.

> [!WARNING]
> If `protoc --version` fails, gRPC is not installed. You MUST install it before proceeding. See Troubleshooting §6.1.

### 2.3 Dataset

We are using a slimmed-down dataset (`data/partitions-small/`) for the live demo. This is highly recommended over the full 13GB dataset because it ensures lightning-fast queries during your 15-minute presentation and saves disk space on your teammate's MacBook.

> [!NOTE]
> The `data/partitions-small/` folder is already committed to the repository. You do NOT need to run the partitioning script for the demo.

### 2.4 Build the C++ binary

Run on **both MacBooks**:

```bash
# From the mini2 root directory:
bash scripts/build.sh
```

**Expected output (last line):**
```
[build] done. binaries in /path/to/mini2/build
```

This produces `build/mini2_server` — the single binary used for all C++ nodes.

### 2.5 Generate Python gRPC stubs

```bash
bash scripts/gen_python_stubs.sh
```

**Expected output:**
```
[gen_python_stubs] generated stubs in /path/to/mini2/python/generated
```

---

## 3. Demo Option A — Single Machine (Localhost)

> **Use this for development, testing, or if you only have one machine.**

### Step 1: Kill any leftover processes

```bash
bash scripts/kill_all.sh
```

**Expected output:**
```
[kill_all] cleared. remaining mini2 processes: 0
```

> [!TIP]
> Always run `kill_all.sh` before starting a fresh demo. Old processes holding ports will prevent new ones from binding.

### Step 2: Create log directory

```bash
mkdir -p experiments/logs
```

### Step 3: Launch all 9 nodes

```bash
bash scripts/launch_all.sh
```

**Expected output:**
```
Starting C++ nodes A through H...
Starting Python node I...
All 9 nodes started in background.
Wait a few seconds for datasets to load...
```

### Step 4: Wait for dataset loading

Each node loads its ~1.4 GB CSV shard at startup. This takes **20-30 seconds**.

```bash
# Wait 30 seconds
sleep 30

# Verify all 9 processes are running:
ps aux | grep -E "mini2_server|python.*server" | grep -v grep | wc -l
```

**Expected output:** `9`

If you see fewer than 9, check the logs:
```bash
# Check which nodes failed:
for n in A B C D E F G H I; do
  if [ -f experiments/logs/${n}.pid ]; then
    pid=$(cat experiments/logs/${n}.pid)
    if ! kill -0 $pid 2>/dev/null; then
      echo "DEAD: node $n (check experiments/logs/${n}.log)"
    fi
  fi
done
```

### Step 5: Verify heartbeat connectivity

```bash
# Check the last few lines of any node's log:
tail -5 experiments/logs/A.log
```

**Expected output** (you should see heartbeat exchanges):
```
[heartbeat] A -> B OK (reply_node=B, reply_ts=...)
[heartbeat] A -> H OK (reply_node=H, reply_ts=...)
[heartbeat] A -> G OK (reply_node=G, reply_ts=...)
[heartbeat] A -> I OK (reply_node=I, reply_ts=...)
```

If you see `FAILED` for any neighbor, that node didn't start properly. See §6.

### Step 6: You're ready to demo!

Continue to [§5 — Demo Scenarios](#5-demo-scenarios--what-to-show).

---

## 4. Demo Option B — Two Machines (LAN)

> **Use this for the final class demo. Requires 2 physical machines on the same network.**

### 4.1 Identify Machine IPs

Both MacBooks must be on the **same WiFi network** (connected to the same router).

On **each MacBook**, open a terminal:
```bash
ipconfig getifaddr en0
```

If that returns nothing (some Macs use a different interface for WiFi):
```bash
ipconfig getifaddr en1
```

**Example:**
- Machine 1 — m1 (your MacBook): `192.168.1.100`
- Machine 2 — m2 (teammate's MacBook): `192.168.1.101`

Write these down — you'll use them to create config files.

### 4.2 Create LAN Config Files

You need to create `config/tree-lan/*.conf` files that use **real IPs** instead of `127.0.0.1`.

**Machine assignment:**
- **m1** runs: A (port 50051), B (50052), C (50053), D (50054)
- **m2** runs: E (50055), F (50056), G (50057), H (50058), I (50059)

```bash
mkdir -p config/tree-lan
```

Now create each config file. **Replace `M1_IP` and `M2_IP` with your actual IPs.**

For example, if m1=`192.168.1.100` and m2=`192.168.1.101`:

```bash
# Helper: set your IPs here
M1_IP="192.168.1.100"
M2_IP="192.168.1.101"

# Generate all LAN configs from the localhost configs
for conf in config/tree/*.conf; do
  node=$(basename "$conf")
  sed \
    -e "s|host=127.0.0.1|host=0.0.0.0|" \
    -e "s|:127.0.0.1:50051|:${M1_IP}:50051|g" \
    -e "s|:127.0.0.1:50052|:${M1_IP}:50052|g" \
    -e "s|:127.0.0.1:50053|:${M1_IP}:50053|g" \
    -e "s|:127.0.0.1:50054|:${M1_IP}:50054|g" \
    -e "s|:127.0.0.1:50055|:${M2_IP}:50055|g" \
    -e "s|:127.0.0.1:50056|:${M2_IP}:50056|g" \
    -e "s|:127.0.0.1:50057|:${M2_IP}:50057|g" \
    -e "s|:127.0.0.1:50058|:${M2_IP}:50058|g" \
    -e "s|:127.0.0.1:50059|:${M2_IP}:50059|g" \
    "$conf" > "config/tree-lan/$node"
done

echo "Created LAN configs in config/tree-lan/"
```

**Verify a config looks right:**
```bash
cat config/tree-lan/A.conf
```

Should show something like:
```
node_id=A
role=gateway
host=0.0.0.0
port=50051
neighbors=B:192.168.1.100:50052,H:192.168.1.101:50058,G:192.168.1.101:50057,I:192.168.1.101:50059
```

> [!IMPORTANT]
> The `host=0.0.0.0` means "listen on all interfaces" — this is required so machines can reach each other over the LAN. The `neighbors` lines use actual IPs.

### 4.3 Set up the teammate's MacBook (m2)

**Option A: Clone + build independently (recommended)**

On the teammate's MacBook:
```bash
# Clone the repo
git clone <your-repo-url> ~/mini2
cd ~/mini2

# Check & install all dependencies
bash scripts/check_deps.sh

# Build C++ binary
bash scripts/build.sh

# Generate Python stubs
bash scripts/gen_python_stubs.sh

> [!IMPORTANT]
> The dataset is already included in the `data/partitions-small/` directory in the repository. You do not need to transfer the massive 13GB dataset!

**Option B: Copy everything from your MacBook**
```bash
# From your MacBook (m1) — copy entire project:
rsync -avz --progress /path/to/mini2/ user@M2_IP:/path/to/mini2/
```

### 4.4 Disable Firewalls

macOS may block incoming gRPC connections. On **both MacBooks**:

```bash
# Option 1: Temporarily disable firewall (easiest for demo)
sudo /usr/libexec/ApplicationFirewall/socketfilterfw --setglobalstate off

# Option 2: Allow the specific binaries
sudo /usr/libexec/ApplicationFirewall/socketfilterfw --add $(pwd)/build/mini2_server
sudo /usr/libexec/ApplicationFirewall/socketfilterfw --add $(which python3)
```

> [!CAUTION]
> Re-enable the firewall on both machines after the demo:
> ```bash
> sudo /usr/libexec/ApplicationFirewall/socketfilterfw --setglobalstate on
> ```

### 4.5 Launch on Machine 1 — MacBook (m1)

Open a terminal on your MacBook:

```bash
cd /path/to/mini2

# Kill anything old
bash scripts/kill_all.sh

mkdir -p experiments/logs

# Start nodes A, B, C, D
for node in A B C D; do
  ./build/mini2_server config/tree-lan/${node}.conf \
    > experiments/logs/${node}.log 2>&1 &
  echo $! > experiments/logs/${node}.pid
  echo "Started $node"
done
```

### 4.6 Launch on Machine 2 — teammate's MacBook (m2)

Open a terminal on the teammate's MacBook:

```bash
cd /path/to/mini2

bash scripts/kill_all.sh

mkdir -p experiments/logs

# Start nodes E, F, G, H (C++)
for node in E F G H; do
  ./build/mini2_server config/tree-lan/${node}.conf \
    > experiments/logs/${node}.log 2>&1 &
  echo $! > experiments/logs/${node}.pid
  echo "Started $node"
done

# Start node I (Python)
python3 python/server.py config/tree-lan/I.conf \
  > experiments/logs/I.log 2>&1 &
echo $! > experiments/logs/I.pid
echo "Started I (Python)"
```

### 4.7 Wait and Verify

Wait **30 seconds** for dataset loading, then verify on each machine:

```bash
# On your MacBook (m1): should show 4 processes
ps aux | grep mini2_server | grep -v grep | wc -l

# On teammate's MacBook (m2): should show 4 C++ + 1 Python = 5 processes
ps aux | grep -E "mini2_server|python.*server" | grep -v grep | wc -l
```

**Quick connectivity test from your MacBook:**
```bash
# Can you reach a port on the teammate's MacBook?
nc -zv <M2_IP> 50055
```
If this fails, check that the firewall is disabled on m2 (§4.4).

Check heartbeats cross the LAN:
```bash
# On MacBook:
grep "heartbeat.*OK" experiments/logs/A.log | tail -5
```

You should see successful heartbeats to nodes on m2 (E, G, H, I).

### 4.8 Run the Client

The client **always connects to A (the gateway)**. From either machine:

```bash
# If running from m1 (A is local):
python3 python/client.py 127.0.0.1:50051 borough eq MANHATTAN 1000

# If running from m2 (A is on m1):
python3 python/client.py <M1_IP>:50051 borough eq MANHATTAN 1000
```

---

## 5. Demo Scenarios — What to Show

> These scenarios work with both localhost and LAN setups. For LAN, replace `127.0.0.1:50051` with `<M1_IP>:50051`.

### Scenario 1: Basic Distributed Query (30 seconds)

**Purpose:** Show that all 9 nodes contribute results to a single query.

```bash
python3 python/client.py 127.0.0.1:50051 borough eq MANHATTAN 1000
```

**What to explain while it runs:**
- "The client submits a query to gateway A asking for all MANHATTAN records"
- "A queries its own shard, then forwards to B, G, H, and I"
- "B further forwards to C, D, E — E forwards to F"
- "Each node processes its local shard and pushes results back up the tree"

**Expected output:**
```
[client] connecting to 127.0.0.1:50051
[client] SubmitQuery: Find borough eq MANHATTAN
[client] QueryAccepted: req_id=A-1 (accepted)
[client] polling FetchChunk for req_id=A-1
  -> chunk 0: 1000 records, done=False, cancelled=False
  -> chunk 1: 1000 records, done=False, cancelled=False
  ...
  -> chunk 4124: 656 records, done=True, cancelled=False
[client] finished. fetched 4124656 records across 4125 chunks.
```

**Key point:** ~4.1 million records returned (aggregate of all 9 shards).

---

### Scenario 2: Query Cancellation (30 seconds)

**Purpose:** Show that cancellation propagates through the entire tree.

```bash
python3 python/cancel_test.py 127.0.0.1:50051 3
```

**What to explain:**
- "Client fetches 3 chunks, then sends CancelQuery"
- "A cancels locally, then sends PeerCancel to B, G, H, I"
- "B receives PeerCancel and forwards it to C, D, E"
- "All nodes immediately stop processing this query"

**Expected output:**
```
[cancel_test] connecting to 127.0.0.1:50051
[cancel_test] SubmitQuery: Find borough eq MANHATTAN
[cancel_test] QueryAccepted: req_id=A-2
[cancel_test] polling FetchChunk, will cancel after 3 chunks
  -> chunk 0: 1000 records, done=False, cancelled=False
  -> chunk 1: 1000 records, done=False, cancelled=False
  -> chunk 2: 1000 records, done=False, cancelled=False

[cancel_test] >>> Sending CancelQuery for A-2 <<<
[cancel_test] CancelAck: cancelled=True
[cancel_test] post-cancel chunk: 0 records, done=True, cancelled=True

[cancel_test] total fetched: 3000 records across 3 chunks before cancel.
```

**Prove it propagated** (show the logs):
```bash
grep "cancel" experiments/logs/*.log
```

Expected:
```
A.log:[query] A cancelled query A-2
B.log:[cancel] B peer-cancelled A-2 (from A)
B.log:[cancel] B propagated PeerCancel -> D for A-2
B.log:[cancel] B propagated PeerCancel -> E for A-2
E.log:[cancel] E peer-cancelled A-2 (from B)
G.log:[cancel] G peer-cancelled A-2 (from A)
I.log:[query] I peer-cancelled A-2
```

---

### Scenario 3: Fairness — Round Robin vs Greedy (1 minute)

**Purpose:** Show that the scheduler affects how concurrent queries are served.

```bash
python3 python/fairness_test.py 127.0.0.1:50051 1000
```

**What to explain:**
- "We launch 3 concurrent queries simultaneously: HUGE (MANHATTAN ~4M), MED (STATEN_ISLAND ~766k), SMALL (Noise-Vehicle ~338k)"
- "Under Round-Robin, small queries finish faster because they get fair turns"

**Expected output:**
```
[fairness] launching 3 concurrent clients against 127.0.0.1:50051
[fairness] all clients done in 3.210s
label                   records   chunks  first_chunk_s    total_s
------------------------------------------------------------------
SMALL-noise-veh          337663      795          0.015      0.891
MED-staten-island        765958     1898          0.048      1.641
HUGE-manhattan          3666627     4269          0.109      3.205
```

**Key point:** Small query finishes in 0.9s even though the huge query is still running — that's fairness!

---

### Scenario 4: Adaptive Chunking (1 minute)

**Purpose:** Show that adaptive mode reduces chunk count and speeds up delivery.

```bash
# Fixed mode (default):
python3 python/adaptive_test.py 127.0.0.1:50051 fixed
```

Then switch configs and restart:
```bash
# Enable adaptive:
for conf in config/tree/*.conf; do
  sed -i '' 's/adaptive_chunking=false/adaptive_chunking=true/' "$conf"
done
bash scripts/kill_all.sh
bash scripts/launch_all.sh
sleep 30

python3 python/adaptive_test.py 127.0.0.1:50051 adaptive

# Restore:
for conf in config/tree/*.conf; do
  sed -i '' 's/adaptive_chunking=true/adaptive_chunking=false/' "$conf"
done
```

**Expected comparison:**
```
Fixed:     4,311 chunks,  avg 2,642 records/chunk,  3.42s total
Adaptive:    734 chunks,  avg 4,995 records/chunk,  2.96s total
```

**Key point:** Adaptive uses 6× fewer chunks and is 13% faster!

---

### Scenario 5: Show the Experiment Plots (1 minute)

**Purpose:** Show pre-generated experiment results.

The plots are already in `report/figures/`:
- `chunk_size.png` — effect of chunk size on latency
- `fairness.png` — round-robin vs greedy comparison
- `local_vs_distributed.png` — single node vs 9-node cluster
- `linear_vs_indexed.png` — indexed vs linear scan

```bash
# Open them (macOS):
open report/figures/chunk_size.png
open report/figures/fairness.png
open report/figures/local_vs_distributed.png
open report/figures/linear_vs_indexed.png
```

If you want to regenerate fresh:
```bash
bash scripts/run_experiments.sh    # ~5 minutes
python3 python/plot_experiments.py  # instant
```

---

### Scenario 6: Request Anticipation (LRU Cache) (30 seconds)

**Purpose:** Show that redundant queries are anticipated and instantly returned from the LRU cache, bypassing the cluster entirely.

```bash
bash scripts/test_cache.sh
```

**What to explain:**
- "We built an LRU cache into the Gateway (Node A) that keys on the serialized `QueryFilter` protobuf."
- "The first run of the query takes normal time to traverse the cluster."
- "The second run hits the cache and returns instantly in zero network hops."

**Expected output:**
You should see `PASS-1`, `PASS-2`, and `PASS-3` indicating that the cache was populated, a hit was registered, and the time was significantly faster.

---

### Scenario 7: Failure Recovery / Janitor Timeout (1 minute)

**Purpose:** Show that if a physical node crashes mid-query, the gateway will not hang indefinitely.

```bash
bash scripts/test_partial.sh
```

**What to explain:**
- "We added an artificial delay to Node B so it acts like it has crashed."
- "The Janitor thread sweeping every 500ms detects that Node B missed its 30-second completion timeout."
- "The gateway forcefully severs the branch, unblocks the query, and returns a `partial=true` response to the client with the ID of the timed-out node."

**Expected output:**
You should see the client log: `partial: timed_out_children=[B]`.

---

## 6. Troubleshooting — Common Errors & Solutions

### 6.1 `protoc: command not found` or CMake can't find gRPC

**Problem:** gRPC/Protobuf not installed.

**Solution:**
```bash
brew install grpc protobuf
```

If `brew install` is slow, try:
```bash
brew install --build-from-source grpc
```

### 6.2 `FATAL: failed to bind [host]:[port] (port in use?)`

**Problem:** Another process is already using that port.

**Solution:**
```bash
# Kill all mini2 processes
bash scripts/kill_all.sh

# If that doesn't work, force-kill everything on those ports:
for p in 50051 50052 50053 50054 50055 50056 50057 50058 50059; do
  lsof -ti :$p | xargs kill -9 2>/dev/null || true
done
```

### 6.3 `No module named 'grpc'` or `No module named 'mini2_pb2'`

**Problem:** Python dependencies not installed or stubs not generated.

**Solution:**
```bash
pip3 install grpcio grpcio-tools protobuf
bash scripts/gen_python_stubs.sh
```

### 6.4 Node starts but crashes immediately

**Problem:** Usually a missing data partition.

**Check:**
```bash
ls -la data/partitions/A.csv
# Should show ~1.4 GB file
```

**Solution:**
```bash
python3 scripts/partition_data.py
```

### 6.5 `FetchChunk failed: [StatusCode.UNAVAILABLE]`

**Problem:** Gateway A is not running or client can't reach it.

**Solution:**
```bash
# Check if A is running:
cat experiments/logs/A.pid | xargs kill -0 2>/dev/null && echo "A running" || echo "A DEAD"

# Check A's log for errors:
tail -20 experiments/logs/A.log
```

### 6.6 Heartbeat shows `FAILED` for some neighbors

**Problem (localhost):** That neighbor node didn't start properly.

**Solution:** Check that node's log:
```bash
cat experiments/logs/<NODE>.log | head -20
```

**Problem (LAN):** Firewall blocking or wrong IP.

**Solution:**
```bash
# Test basic connectivity from your MacBook:
nc -zv <M2_IP> 50055

# If blocked, disable macOS firewall on BOTH machines:
sudo /usr/libexec/ApplicationFirewall/socketfilterfw --setglobalstate off
```

### 6.7 LAN demo: `Connection refused` errors

**Problem:** Node is listening on `127.0.0.1` instead of `0.0.0.0`.

**Solution:** Make sure the LAN configs have `host=0.0.0.0` (not `host=127.0.0.1`). Re-run the config generation script from §4.2.

### 6.8 Query returns fewer records than expected

**Problem:** Some nodes didn't start, or they crashed during the query.

**Check:**
```bash
# Count running processes:
ps aux | grep -E "mini2_server|python.*server" | grep -v grep | wc -l
# Should be 9

# Check which nodes are dead:
for n in A B C D E F G H I; do
  pid=$(cat experiments/logs/${n}.pid 2>/dev/null)
  if [ -z "$pid" ] || ! kill -0 "$pid" 2>/dev/null; then
    echo "DEAD: $n"
  fi
done
```

### 6.9 `rsync` to second machine is extremely slow

**Problem:** Copying 13 GB of partitions over WiFi.

**Solution:**
- Use ethernet cable between machines
- Or partition data on each machine independently:
  ```bash
  # On m2:
  python3 scripts/partition_data.py
  ```
  This requires `dataset/311_2020.csv` to be on m2 as well.

### 6.10 Python node I doesn't respond

**Problem:** Python server failed to start.

**Solution:**
```bash
# Check I's log:
cat experiments/logs/I.log

# Common fix: regenerate stubs
bash scripts/gen_python_stubs.sh

# Try running I manually to see errors:
python3 python/server.py config/tree/I.conf
```

---

## 7. Expected Results Reference

### Record Counts by Borough (full dataset, 9-node cluster)

| Borough | Approximate Records |
|---|---|
| MANHATTAN | ~4,124,656 |
| BROOKLYN | ~4,800,000 |
| BRONX | ~3,200,000 |
| QUEENS | ~3,500,000 |
| STATEN_ISLAND | ~766,000 |

> [!NOTE]
> Record counts may vary slightly due to hash-based partitioning. The exact total depends on the dataset version.

### Single-node (A only) vs Full cluster

| Mode | Records | Time |
|---|---|---|
| A only | ~458,000 | ~260 ms |
| Full cluster (9 nodes) | ~4,125,000 | ~2.7 s |

The distributed version returns **8× more data** because it aggregates from all 9 shards.

### Timing Reference (localhost, borough=MANHATTAN)

| Test | Expected Time |
|---|---|
| Basic query (1000 records/chunk) | 2.5–4 seconds |
| Cancel test (3 chunks) | < 1 second |
| Fairness test (3 clients) | 3–4 seconds total |
| Adaptive test | 2.5–3 seconds |

---

## 8. Quick-Reference Commands

### Lifecycle

```bash
# Build
bash scripts/build.sh

# Generate Python stubs
bash scripts/gen_python_stubs.sh

# Start all 9 nodes (localhost)
bash scripts/launch_all.sh

# Stop all nodes
bash scripts/kill_all.sh
```

### Testing

```bash
# Basic query (Python client)
python3 python/client.py 127.0.0.1:50051 borough eq MANHATTAN 1000

# Basic query (C++ client) — same RPC contract
./build/mini2_client 127.0.0.1:50051 borough eq MANHATTAN 1000

# Cancel test
python3 python/cancel_test.py 127.0.0.1:50051 3

# Fairness test (3 concurrent clients)
python3 python/fairness_test.py 127.0.0.1:50051 1000

# Adaptive chunking test
python3 python/adaptive_test.py 127.0.0.1:50051 fixed

# Run all 4 experiments + generate plots
bash scripts/run_experiments.sh
python3 python/plot_experiments.py
```

### Debugging

```bash
# Check how many nodes are running
ps aux | grep -E "mini2_server|python.*server" | grep -v grep | wc -l

# View a node's log
tail -50 experiments/logs/A.log

# Search for errors across all logs
grep -i "error\|fail\|fatal" experiments/logs/*.log

# Search for cancel propagation
grep "cancel" experiments/logs/*.log

# Check janitor activity (TTL expiry, cleanup)
grep "janitor" experiments/logs/*.log

# Force-kill everything
bash scripts/kill_all.sh
```

### Config Tweaking

```bash
# Switch to greedy scheduler:
for f in config/tree/*.conf; do
  sed -i '' 's/scheduler_policy=round_robin/scheduler_policy=greedy/' "$f"
done

# Switch back to round_robin:
for f in config/tree/*.conf; do
  sed -i '' 's/scheduler_policy=greedy/scheduler_policy=round_robin/' "$f"
done

# Enable adaptive chunking:
for f in config/tree/*.conf; do
  sed -i '' 's/adaptive_chunking=false/adaptive_chunking=true/' "$f"
done

# Disable adaptive chunking:
for f in config/tree/*.conf; do
  sed -i '' 's/adaptive_chunking=true/adaptive_chunking=false/' "$f"
done
```

> [!IMPORTANT]
> After changing any config, you must **restart the cluster** for changes to take effect:
> ```bash
> bash scripts/kill_all.sh
> bash scripts/launch_all.sh
> sleep 30
> ```

---

## Demo Flow Suggestion (5–7 minute presentation)

1. **[30s]** Show the architecture diagram. Explain tree topology, A is gateway.
2. **[30s]** Launch the cluster: `bash scripts/launch_all.sh`
3. **[30s]** Run a basic query: `python3 python/client.py ...`
   - Point out the ~4.1M records aggregated from 9 nodes
4. **[30s]** Run cancellation: `python3 python/cancel_test.py ...`
   - Show `cancelled=True` response
   - `grep cancel experiments/logs/*.log` to prove tree-wide propagation
5. **[60s]** Run fairness test: `python3 python/fairness_test.py ...`
   - Explain how RR ensures small queries aren't starved
6. **[60s]** Show the experiment plots
   - Open `report/figures/*.png` — explain each finding
7. **[30s]** Clean up: `bash scripts/kill_all.sh`
