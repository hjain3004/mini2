# CMPE 275 — Mini 2 Final Presentation Script

**Format:** 15-minute live demo + slide walkthrough · 2 presenters · 2 MacBooks on the same LAN
**Single slide reference:** `slide_prompt.md` (KPI tiles · topology · 3 perf charts · 11-row spec checklist)
**Spec under audit:** `mini2-chunks.md`

---

## 0 · Pre-flight (before the prof walks over)

> **Both laptops already have the cluster running.** Do NOT relaunch during the demo. Just verify, then drive client commands during the talk.

**m1 (gateway machine, runs A, B, C, D):**
```bash
cd ~/275/mini2
bash scripts/kill_all.sh
mkdir -p experiments/logs
for node in A B C D; do
  ./build/mini2_server config/tree-lan/${node}.conf \
    > experiments/logs/${node}.log 2>&1 &
  echo $! > experiments/logs/${node}.pid
done
# verify
ps aux | grep mini2_server | grep -v grep | wc -l   # expect 4
ipconfig getifaddr en0                              # note the IP — call this M1_IP
```

**m2 (worker machine, runs E, F, G, H + Python I):**
```bash
cd ~/275/mini2
bash scripts/kill_all.sh
mkdir -p experiments/logs
for node in E F G H; do
  ./build/mini2_server config/tree-lan/${node}.conf \
    > experiments/logs/${node}.log 2>&1 &
  echo $! > experiments/logs/${node}.pid
done
python3 python/server.py config/tree-lan/I.conf \
  > experiments/logs/I.log 2>&1 &
echo $! > experiments/logs/I.pid
# verify
ps aux | grep -E "mini2_server|python.*server" | grep -v grep | wc -l   # expect 5
```

**Both laptops, wait ~30 s for shards to load, then sanity-check heartbeats:**
```bash
grep "heartbeat.*OK" experiments/logs/A.log | tail -5   # m1
grep "heartbeat.*OK" experiments/logs/I.log | tail -5   # m2
```

You should see successful heartbeats crossing the LAN. If any neighbor shows `FAILED`, refer to `DEMO_GUIDE.md` §6.6 (firewall / wrong IP). The whole cluster must be green before showtime.

**Terminal layout for the demo itself:**
- **m1, tab 1** — slide (Claude Design export, fullscreen)
- **m1, tab 2** — client command line (most demos run here)
- **m1, tab 3** — `tail -f experiments/logs/A.log` (gateway live log)
- **m2, tab 1** — `tail -f experiments/logs/I.log` (proves Python leaf is alive)
- **m2, tab 2** — `tail -f experiments/logs/B.log` *or* a window to manually SIGKILL a node for the failure demo

---

## ⏱ 15-Minute Timing Budget

| Phase | Topic | Time | Slide region |
|---|---|---|---|
| 1 | Architecture pitch | 1:30 | Topology + spec checklist |
| 2 | Scatter-gather live | 1:30 | Headline tile: 4.1 M / 2.7 s |
| 3 | Manual chunking & no-streaming | 1:00 | Spec row 2 |
| 4 | Fairness — RR vs Greedy | 2:30 | `fairness_compact.png` |
| 5 | Adaptive chunking | 1:30 | Headline tile: 83 % |
| 6 | Indexed vs Linear (selectivity) | 1:30 | `linear_vs_indexed.png` |
| 7 | Mid-flight cancellation | 1:00 | Spec checklist |
| 8 | Request anticipation (cache) | 1:30 | `cache_cold_vs_warm.png` |
| 9 | Failure recovery (partial) | 1:30 | Spec checklist |
| 10 | Closing — checklist & source-code offer | 1:00 | Whole slide |
| **Total** | | **14:30** | (~30 s reserve for questions in flight) |

---

## Phase 1 · Architecture Pitch — 1:30
*Goal: Establish that you built **exactly** what the spec asked for — no shortcuts.*

**Slide focus:** topology diagram (left half of slide) and the 11-row spec checklist (right half).

**Script:**
> *"Professor, this is Mini 2 — a nine-process distributed query engine over a gRPC tree overlay, running across these two MacBooks on the same Wi-Fi network. The data is the NYC 311 Service Requests dataset — 12.7 gigabytes, roughly 33 million records, hash-sharded into nine disjoint partitions. No replication. No mirroring.*
>
> *Looking at the diagram: Node A is the only gateway. Only A accepts external client RPCs. We deployed A, B, C, D on m1 (this laptop), and E, F, G, H, plus the Python leaf I on m2. The solid edges form the spanning tree exactly as specified in the prompt — A–B, B–C, B–D, B–E, E–F, A–H, A–G, A–I. The two dashed edges, E–D and E–G, are the cycles your spec hinted at; we handle them with a `visited_nodes` guard that travels in every PeerQuery — if a node sees its own ID in that list, it drops the request.*
>
> *On the right is the eleven-item compliance checklist. Every line is a constraint from your spec, paired with where it lives in the code. We can drill into any of them on request."*

**Show:** point at the green checkmarks on the slide. If asked to prove no hardcoding:
```bash
# m1
cat config/tree-lan/A.conf
```
Read out the `neighbors=` line — every peer is reachable only through that config string.

---

## Phase 2 · Scatter-Gather Live — 1:30
*Goal: Run the headline query, prove 9 shards independently contribute.*

**Slide focus:** the **4.1 M** KPI tile (top-left).

**Run on m1 tab 2:**
```bash
./build/mini2_client <M1_IP>:50051 borough eq MANHATTAN 1000
```

**While it runs, on m1 tab 3 you'll see hundreds of `[query] A sending chunk N for A-1` lines, and on m2 tab 1 you'll see `[query] I sending chunk …` from the Python leaf.**

**Script (during the ~3 s the command runs):**
> *"This is the C++ client — the spec required both a C++ server **and** a C++ client. It's calling `SubmitQuery` on the gateway, then polling `FetchChunk` in a unary loop. Watch the chunks scroll on this side.*
>
> *Notice the result count: 4.1 million records, all five boroughs of Manhattan, returned in roughly 2.7 seconds. That's the aggregate of all nine shards — gateway A contributed ~458 thousand from its local partition, B's subtree contributed another large slice, and the Python leaf I added its hash bucket. The system gathers, deduplicates against `visited_nodes`, and serves the merged result through one channel — A."*

**Point at the slide's headline tile:** *"4.1 million records, 2.7 seconds, end-to-end."*

If asked "how do you know it actually used the Python leaf?", switch to m2 tab 1 and show:
```bash
grep "I sending chunk\|I local_done" experiments/logs/I.log | tail -5
```

---

## Phase 3 · Manual Chunking & The No-Streaming Rule — 1:00
*Goal: Address the prof's most-emphasized constraint head-on.*

**Slide focus:** spec checklist row 2 (*gRPC unary only — no streaming*).

**Script:**
> *"You explicitly forbade gRPC's async and streaming APIs. We took that constraint seriously — every one of our seven RPCs is unary. The client side is a polling loop; the server side never returns a stream object. We built our own `ChunkManager` that bounds every payload by two constraints simultaneously:*
> 1. *A configurable maximum record count (default 500, here 1,000), and*
> 2. *A hard byte budget — 65 kilobytes — that wins if a record set has long string fields.*
>
> *The byte budget is what protects us from blowing up gRPC's 4-megabyte message limit. The record cap is what gives us the fairness lever we'll discuss next. Both are visible in `cpp/src/chunk/ChunkManager.cpp` if you want to see the source."*

**Show (optional, if you have time):**
```bash
grep "chunk" config/tree-lan/A.conf
```
Read out `default_chunk_records=500`, `max_chunk_bytes=65536`.

---

## Phase 4 · Fairness — Round-Robin vs Greedy — 2:30
*Goal: Address the spec's `fairness between end-points` requirement with measured evidence — and pre-empt the "why is MED slower under RR?" question.*

**Slide focus:** `fairness_compact.png` (bottom-left chart).

**Run on m1 tab 2:**
```bash
python3 python/fairness_test.py <M1_IP>:50051 1000
```

This fires three concurrent clients — SMALL (~338 K records, "Noise – Vehicle"), MED (~766 K, Staten Island), HUGE (~3.7 M, Manhattan) — under the currently configured policy (round-robin).

**Script (during the ~3 s run):**
> *"This is the fairness experiment. Three concurrent clients hit the gateway at the same instant with queries of three different cardinalities — small, medium, and huge. The scheduler at A interleaves their chunk-pushes.*
>
> *Look at the chart on the slide. The green bars are round-robin, the red are greedy. Two takeaways:*
>
> *First — SMALL finishes in **0.89 seconds under round-robin versus 1.29 under greedy**. That's a 31 percent improvement. The small query no longer has to wait behind the huge one to get any bandwidth.*
>
> *Second — and this is the interesting one — MED actually finishes **faster under greedy** in this run. That isn't a bug; it's the precise tradeoff round-robin makes. Greedy drains one request to completion, then the next. In greedy mode, MED was lucky and got drained second, with the entire chunk-pusher to itself for one uninterrupted second. Under round-robin, MED never gets that uninterrupted runway — it shares bandwidth with HUGE for its entire lifetime.*
>
> *So round-robin is fair in the worst-case sense — it eliminates starvation of the smallest query — but it doesn't Pareto-dominate. Mid-tier queries pay the contention tax. We made the policy a configuration knob: `scheduler_policy=round_robin` or `greedy`, restart, done. That way the operator can pick the latency profile that matches their workload."*

**If you want to flip live (optional, costs ~45 s, only if ahead of schedule):**
```bash
# m1 — flip the scheduler then restart only m1 nodes
for f in config/tree-lan/{A,B,C,D}.conf; do
  sed -i '' 's/scheduler_policy=round_robin/scheduler_policy=greedy/' "$f"
done
bash scripts/kill_all.sh && \
  for n in A B C D; do
    ./build/mini2_server config/tree-lan/${n}.conf > experiments/logs/${n}.log 2>&1 &
  done
sleep 20
python3 python/fairness_test.py <M1_IP>:50051 1000
# (revert sed before continuing)
```

If short on time, just point at the chart and move on.

---

## Phase 5 · Adaptive Chunking — 1:30
*Goal: Cover the spec's `parallel and dynamic chunk sizing` requirement.*

**Slide focus:** the **83 %** KPI tile.

**Script (no command needed — explain from the slide):**
> *"The spec asked for adaptive payload sizing on the fly. Our adaptive chunking is closed-loop control: after every `PushPeerChunk`, the scheduler measures the round-trip latency. If it's under 20 milliseconds and the queue depth is greater than five thousand records, we **double** the next chunk size, capped at 5,000. If latency goes above 200 milliseconds, we **halve** it, floored at 100. It's TCP-style additive-increase-multiplicative-decrease applied at the application layer.*
>
> *On a 3.7-million-record query the algorithm converges almost immediately to the 5,000-record cap — sub-20-millisecond loopback latency confirms there's plenty of room. The numbers: fixed mode emits 4,311 chunks; adaptive mode emits 734. **Eighty-three percent fewer RPC round-trips, thirteen percent faster wall-clock time.***
>
> *That's also why the byte budget I mentioned earlier matters — even when adaptive says 'go to 5,000 records,' the 65-kilobyte budget can preempt and emit a shorter chunk. The two mechanisms cooperate."*

If asked to demonstrate live, run (~10 s):
```bash
python3 python/adaptive_test.py <M1_IP>:50051 adaptive
```

---

## Phase 6 · Indexed vs Linear — Selectivity Matters — 1:30
*Goal: Cover the spec's `move away from linear searching` requirement.*

**Slide focus:** `linear_vs_indexed.png` (bottom-right chart).

**Script (no command needed):**
> *"Each shard builds four secondary indexes at startup — a hash map for borough, a hash map for complaint type, an ordered map on created date, and a 0.01-degree coarse grid on lat/long. The local query engine picks the most selective indexed predicate available before falling back to a linear scan.*
>
> *We ran a controlled experiment on node A alone — full 3.7-million-row shard, no network — comparing the two paths on a deliberately selective predicate: `complaint_type` equals "Noise – Helicopter", which matches just 19 thousand rows, about half a percent of the shard.*
>
> *Look at the chart. Indexed: 7.4 milliseconds to first chunk, 15.2 milliseconds total. Linear: 22.7 milliseconds to first chunk, 30.3 milliseconds total. **The index gives us a 3.07× speedup on first-chunk latency and a 1.99× speedup overall** — exactly what you'd predict from skipping the 3.7-million-row scan.*
>
> *Important caveat: the speedup is selectivity-dependent. If we ran this with `borough = MANHATTAN`, which matches 10 percent of rows, the speedup collapses to about 1.12×. The index avoids touching irrelevant rows; if there aren't many irrelevant rows, the saving is small. We have both data points in the report."*

---

## Phase 7 · Mid-Flight Cancellation — 1:00
*Goal: Answer the spec's `client cancels before completion` rhetorical.*

**Run on m1 tab 2:**
```bash
python3 python/cancel_test.py <M1_IP>:50051 3
```

This client fetches 3 chunks, then issues `CancelQuery`.

**Script (during the ~1 s run):**
> *"This client deliberately abandons after three chunks. Watch what happens in A's log — A flips the request state to CANCELLED, drops its result queue, then fans out `PeerCancel` to every child. Each child flips its own state, then forwards the cancel further down. Every active query gets garbage-collected and every worker thread sees the cancelled flag and short-circuits its loop."*

**Pop to m2 tab 1 and show:**
```bash
grep "cancel" experiments/logs/*.log | head -20
```
Point at the lines from B, E, F — the cancel wave reached the far side of the tree within milliseconds.

> *"Cancellation propagates tree-wide. Nothing leaks. Nothing hangs."*

---

## Phase 8 · Request Anticipation — The LRU Cache — 1:30
*Goal: Answer the spec's rhetorical: `Can requests be anticipated?`*

**Slide focus:** `cache_cold_vs_warm.png`.

**Run on m1 tab 2:**
```bash
bash scripts/test_cache.sh
```

**Script (during the ~8 s run):**
> *"You asked in the spec — can requests be anticipated? Yes. We built a small LRU result cache at the gateway, keyed on the **serialized binary** of the `QueryFilter` protobuf. Same predicate, same key, byte-for-byte.*
>
> *Run 1 of the test does the full scatter-gather, then populates the cache. Run 2 hits the cache key and **skips the peer fan-out entirely** — no `PeerQuery` to B, G, H, or I. Instead the gateway shares the cached `std::shared_ptr<vector<ServiceRecord>>` straight into a new `ActiveQuery` and serves it through the same `FetchChunk` polling loop.*
>
> *On the chart: **cold run 3.90 seconds, warm run 3.11 seconds**. That's a 20 percent end-to-end speedup. The remaining 3.1 seconds is RPC overhead from the client polling 4,000-plus chunk fetches — that cost is paid in both modes, cache or no cache. What the cache eliminates is the scatter-gather coordination phase, which on a healthy LAN is a few hundred milliseconds.*
>
> *Cache is bounded — five entries by default, LRU eviction. In production we'd add TTL or dataset-version keying; here we keep it lifetime-of-process for the demo."*

**Optional grep to prove the cache fired:**
```bash
grep "CACHE" experiments/logs/A.log | tail -4
```

---

## Phase 9 · Failure Recovery — The Janitor — 1:30
*Goal: Answer the spec's bonus question about node death.*

**Slide focus:** spec checklist row "Failure recovery (per-child timeout)."

**Pre-arrange:** m2 tab 2 ready to SIGKILL node B. Get B's PID first:
```bash
# m2 tab 2
cat ~/275/mini2/experiments/logs/B.pid
# remember the number
```

Actually, B runs on **m1** in your split. So the kill happens on **m1 tab 4** (or wherever you keep a spare shell).

**Sequence:**
1. **m1 tab 2** — start a query: `./build/mini2_client <M1_IP>:50051 borough eq MANHATTAN 1000`
2. **m1 tab 4** — within 1 second: `kill -9 $(cat experiments/logs/B.pid)`
3. **m1 tab 3** — watch A's log for the janitor entry.

**Script (during the live kill):**
> *"This is the spec's optional question — what if a node dies mid-query? Without a safety net, the gateway would wait forever for `source_done` from B's subtree, because the spec uses unary RPCs and there's no socket-level error.*
>
> *We built a `peer_completion_timeout_ms` — defaults to 30 seconds, configurable. The TTL janitor wakes every 500 milliseconds, scans active queries, and force-completes any expected child that has missed the deadline. The request is then marked **partial**, and on the next `FetchChunk` the gateway surfaces a message like `timed_out_children=[B]` so the client knows the result set is incomplete.*
>
> *I'm killing B right now — `SIGKILL`, no graceful shutdown. Watch A's log."*

Show the log line:
```
[janitor] A peer-completion-timeout: force-completing child B for A-1 (age=...)
```

> *"There it is. A force-completed B's subtree, closed out the request cleanly with `partial=true`, and the client got its results — minus B's slice — instead of hanging forever. We verified this in the report against the live 4-million-record query: A had already emitted 4,124 record-bearing chunks before B died; everything that had landed before the kill was delivered."*

**Cleanup (after the demo phase):** restart B so the cluster is whole again:
```bash
./build/mini2_server config/tree-lan/B.conf > experiments/logs/B.log 2>&1 &
echo $! > experiments/logs/B.pid
```

---

## Phase 10 · Closing — Compliance Volley & Source Offer — 1:00

**Slide focus:** entire slide.

**Script:**
> *"To recap against your spec, professor — every constraint, every rhetorical question:*
>
> - *No hardcoding? **Configs only, two sets — `tree/` for localhost, `tree-lan/` for the demo.***
> - *No streaming? **Seven unary RPCs, manual chunk tracking.***
> - *Tree topology with cycles? **Strict edges, `visited_nodes` cycle guard.***
> - *Only A talks to the client? **Yes — peers use `PushPeerChunk` upward.***
> - *Typed records? **`int64`, `double`, `uint32`. Enums via `uint32`.***
> - *Two-machine deployment? **You're looking at it.***
> - *Polyglot? **Eight C++ nodes plus a Python leaf, same proto contract.***
> - *Fairness? **Round-robin scheduler, 31 percent faster small queries.***
> - *Adaptive chunking? **AIMD on RPC latency, 83 percent fewer pushes.***
> - *Indexed lookup? **4 secondary indexes, 3.07× on selective predicates.***
> - *Cancellation? **`PeerCancel` wave, tree-wide propagation.***
> - *Request anticipation? **LRU cache on serialized filter, 20 percent end-to-end win.***
> - *Failure recovery? **`peer_completion_timeout_ms` + janitor, partial results.***
>
> *Everything you see is reproducible end-to-end. The code is on GitHub, the experiments are scripted, the figures regenerate from CSV. I'd be happy to walk you through the scheduler driver loop, the cache key construction, or the cycle-guard inside `PeerQuery` if you'd like — just say the word."*

---

## Anticipated Questions — Quick Cheat Sheet

| Question | One-line answer |
|---|---|
| *Why is MED slower under round-robin?* | Greedy gives whichever request gets picked second the whole pool to itself; RR shares bandwidth equally for the entire MED lifetime. RR optimizes worst-case latency, not unweighted throughput. |
| *Why only 1.99× total speedup with the index when first-chunk is 3.07×?* | Index saves the scan cost; serialization + chunk-push cost is paid in both modes. |
| *Why does the warm cache run still take 3 seconds?* | Client polls FetchChunk thousands of times. The cache eliminates scatter-gather; the polling RPCs still happen. |
| *Why hash sharding instead of range?* | Hash gives uniform distribution; we acknowledge in the report that hot-key ranges become a hot-spot. Future work. |
| *Does cancel actually kill the worker thread?* | No — workers see `aq.cancelled` and short-circuit. Killing threads mid-scan would leak resources. |
| *What if A itself crashes?* | Single point of failure today. Future work: replicated gateway with shared `RequestTracker`. |
| *Why no streaming?* | Your spec forbade it. We agree: streaming would hide the chunk-control flow we were graded on. |
| *Is `visited_nodes` O(N²)?* | It's a small `vector<string>` of node IDs (≤9). Linear scan is trivially fast at this size; would switch to `unordered_set` at scale. |
| *Two MacBooks on Wi-Fi — what's the RTT?* | ~1–3 ms loopback, ~2–8 ms LAN. The adaptive chunker hits the 20 ms threshold easily and goes to the 5,000-record cap. |

---

## Post-Demo Cleanup

```bash
# Both laptops
bash scripts/kill_all.sh

# Re-enable firewalls if they were disabled
sudo /usr/libexec/ApplicationFirewall/socketfilterfw --setglobalstate on
```

End the demo with: *"Happy to show source for any subsystem — scheduler, cache, janitor, or the cycle-safe routing in PeerQuery."*
