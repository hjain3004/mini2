# Mini 2 — Gap Analysis vs Prof's Spec (`mini2-chunks.md`)

## Context

You've finished implementation and a 2-machine demo, your teammate drafted `main.tex`, and figures live in `report/figures/`. This document audits the implementation, results, and report against the prof's spec (`mini2-chunks.md`) and produces a prioritized fix list before submission.

Sources cross-checked: `mini2-chunks.md` (spec), `DEMO_GUIDE.md` (what you ran), `main.tex` (report draft), `PROGRESS.md`, `proto/mini2.proto`, `cpp/src/`, `python/`, `report/figures/`, `experiments/`.

---

## 1. Requirement-by-requirement assessment

Legend: ✅ done well · 🟡 done but weak · ❌ missing · ⚠️ inaccurate in report

### Core mechanics

| # | Prof's requirement | Status | Notes |
|---|---|---|---|
| 1 | Distributed data + scatter-gather | ✅ | 9 shards (`hash(unique_key)%9`), aggregate 3.7M–4.1M records end-to-end. |
| 2 | gRPC, NO async/streaming APIs | ✅ | All 7 RPCs unary. Client polls `FetchChunk`. |
| 3 | Python AND C++ servers | ✅ | Node I = Python, A–H = C++. |
| 4 | **C++ client** | ❌ | Spec: *"a server and a client written in C++, and also a server in Python."* You have `python/client.py` only. `main.tex` line 377 even claims a `mini2_client` binary that doesn't exist. **Must-fix.** |
| 5 | Tree overlay AB,BC,BD,BE,EF,ED,EG,AH,AG,AI | ✅ | Matches prof's edge list exactly; `visited_nodes` cycle guard verified. |
| 6 | Only A talks to client | ✅ | `SubmitQuery`/`FetchChunk`/`CancelQuery` accepted only on A. |
| 7 | No hardcoded settings | ✅ | `config/tree/*.conf` + `config/tree-lan/*.conf`, plain `key=value`. |
| 8 | Typed data structures (not strings-for-everything) | ✅ | `ServiceRequest` proto uses int64/double/uint32; enums as uint32. |
| 9 | Independent shard contributions, no replication | ✅ | Disjoint shards by hash. |
| 10 | 2-machine demo | ✅ | You verified per `DEMO_GUIDE.md` §4. PROGRESS.md Step 12 is stale (still unchecked). |

### Optimization / fairness / chunks

| # | Requirement | Status | Notes |
|---|---|---|---|
| 11 | Chunked delivery (manual, no streaming) | ✅ | `ChunkManager` with record + byte budget. |
| 12 | Dynamic/adaptive chunk sizing | ✅ | Config-flagged, 83% fewer chunks, 13% faster. |
| 13 | Fairness between endpoints | ✅ | Round-robin vs greedy comparison; SMALL query 31% faster under RR. |
| 14 | Cancellation mid-query | ✅ | `CancelQuery` + `PeerCancel` propagation verified in logs. |
| 15 | Client abandons / disconnects | ✅ | TTL + abandonment janitor (500ms). |
| 16 | Result set too big | ✅ | Chunked polling handles it. |
| 17 | "Can requests be anticipated?" | ❌ | Rhetorical prompt in spec, not addressed in code or report. Optional — see §3. |

### Indexing / non-linear scan

| # | Requirement | Status | Notes |
|---|---|---|---|
| 18 | "Move away from linear searching" | 🟡 | 4 indexes built (borough, complaint, created_date, geo-grid). Indexed vs linear experiment shows only **1.12× speedup** because `borough` is coarse — prof may want a more selective demo (e.g., `complaint_type=eq` over a rare type, or a geo-bbox query). |

### Reporting / hygiene

| # | Requirement | Status | Notes |
|---|---|---|---|
| 19 | Logical sub-directory organization | ✅ | `cpp/`, `python/`, `proto/`, `scripts/`, `config/`, `experiments/`. |
| 20 | No IDE/VM testing | ✅ | All via shell scripts. |
| 21 | KPIs measured | ✅ | 5 experiments, CSVs in `experiments/`, 4 plots. |
| 22 | Report | 🟡 | `main.tex` is well-structured IEEE format but has 3 inaccuracies (see §2). |

---

## 2. Report (`main.tex`) — specific inaccuracies / weak spots

These are claims in `main.tex` that don't match the code/results. Fix before final PDF.

1. **Line 377 — phantom `mini2_client` binary.** Says "links `mini2_server` and `mini2_client` binaries". No C++ client exists. Either drop the claim or build the binary (see §3 Fix #1).
2. **Line 234 — `QueryFilter` op list says "six operators".** Lists only `eq, gt, lt, between, geo_bbox` (five). Either add the sixth or change to "five".
3. **§3.4 module inventory — `RequestTracker` / `IndexSet` naming.** Verify these names match the code (`cpp/include/` headers). If the codebase uses different class names, the report should reflect them.
4. **§9 Discussion — Python GIL claim.** Says Python node's "chunk throughput is somewhat lower… due to GIL." No measurement supports this in the experiments. Either add a small benchmark or soften to "expected to be lower".
5. **Abstract & §1 — author count.** Single author listed. If this is a team submission, add teammates.
6. **§8.4 Adaptive — numbers vs Tab.7.** Tab.4 claims 5000 records at `chunk_records=5000` produced 2,953 chunks (super-linear); explain the byte-budget fragmentation more explicitly in the caption, since the table footnote is the only place that mentions it.
7. **No discussion of how the team split nodes.** Prof's example was Team Blue (A,B,D,H) / Yellow (C,E,F,G,I); your demo split was m1=A,B,C,D / m2=E,F,G,H,I. Cosmetic but worth a sentence in §3.3 acknowledging the choice.
8. **PROGRESS.md Step 12 & 13 unchecked.** Submission hygiene — flip to `[x]` with one-line narratives.

---

## 3. Prioritized fix list

### P0 — Must-fix (prof requirement violation)

**Fix #1: Add a C++ client (`mini2_client`)**
- Create `cpp/src/main_client.cpp` mirroring `python/client.py`:
  - Connect to A's gateway address (from argv or config).
  - `SubmitQuery` → loop `FetchChunk` until `done=true` → print records.
  - Optional: support `CancelQuery` via Ctrl-C handler.
- Add target in `CMakeLists.txt` (the report already advertises the binary, so naming aligns).
- Reuse generated stubs in `cpp/generated/`.
- Smoke test: `./build/mini2_client 127.0.0.1:50051 borough eq MANHATTAN 1000` returns same ~4.1M record count as Python client.
- Update `DEMO_GUIDE.md` §8 quick-reference with the C++ client invocation.
- Estimated effort: ~1–1.5 hr.

### P1 — Strongly recommended (closes report inaccuracies)

**Fix #2: Make the linear-vs-indexed experiment more compelling**
- Re-run §20.5 using a more selective predicate (e.g., `complaint_type=eq "Noise - Helicopter"` or a small geo-bbox) where the index actually skips most rows.
- Expected: speedup 5×–50× instead of 1.12×. Replot `linear_vs_indexed.png` and update Table 5 in `main.tex` §8.3.
- Alternative: keep current numbers but rename the section to "Selectivity of indexed lookup" and explicitly note borough is low-cardinality.
- Estimated effort: ~30 min if cluster is already running.

**Fix #3: Resolve `main.tex` inaccuracies (items 1–6 in §2 above)**
- Coordinate with teammate. Mostly small text edits + one number check.
- Estimated effort: ~30 min.

### P2 — Nice-to-have (optional, only if time permits)

**Fix #4: "Anticipating requests" (§ in spec)**
- Cheapest interpretation: a tiny LRU result cache keyed on `(field, op, value)` at A. Cache the chunk list; on hit, replay from cache. Add a §10 paragraph in the report.
- Skip if short on time; prof framed this as a question, not a requirement.

**Fix #5: Failure-recovery sketch**
- Prof said optional. The current code blocks waiting for `source_done` if a child crashes. Add a per-child timeout (e.g., 30s) that marks the child done and emits a `partial=true` warning to the client. Mention in `main.tex` §9 Limitations or §10 Future Work.
- Estimated effort: ~1 hr if pursued; otherwise leave as Future Work.

**Fix #6: Housekeeping**
- Update `PROGRESS.md` Steps 12 & 13 to `[x]` with a sentence each.
- Add `report/mini2_report.md` if prof wants the markdown form (§24 of the original implementation plan), OR confirm with prof that the IEEE PDF is acceptable.

---

## 4. Verification checklist before submission

Run these in order; each must pass:

1. `bash scripts/kill_all.sh && bash scripts/launch_all.sh && sleep 30`
2. `python3 python/client.py 127.0.0.1:50051 borough eq MANHATTAN 1000` → ~4.1M records
3. `./build/mini2_client 127.0.0.1:50051 borough eq MANHATTAN 1000` → same count *(new, after Fix #1)*
4. `python3 python/cancel_test.py 127.0.0.1:50051 3` → `cancelled=True` and `grep cancel experiments/logs/*.log` shows tree-wide propagation
5. `python3 python/fairness_test.py 127.0.0.1:50051 1000` → SMALL finishes before HUGE under round-robin
6. Compile `main.tex` → confirm 4 figures render, no undefined refs
7. Two-machine smoke: per `DEMO_GUIDE.md` §4, run on m1+m2, query from either side returns full ~4.1M.

---

## 5. Files this assessment touches (read-only summary)

- **Spec source of truth:** `mini2-chunks.md`
- **Implementation evidence:** `proto/mini2.proto`, `cpp/src/grpc/ServiceImpl.cpp`, `cpp/src/scheduler/Scheduler.cpp`, `python/server.py`, `python/client.py`
- **Results:** `experiments/{chunk_size,fairness,linear_vs_indexed,local_vs_distributed,adaptive}/`, `report/figures/*.png`
- **Report:** `main.tex`
- **Hygiene:** `PROGRESS.md`, `DEMO_GUIDE.md`

## 6. Bottom line

You're at roughly **90% of prof's spec** with strong execution on the hard parts (cycle-safe forwarding, manual chunking, RR fairness, adaptive sizing, cancellation, polyglot, typed schema). The one **hard gap** is the missing C++ client — that's a black-and-white requirement and your report already implicitly promises it. The **soft gaps** are a weak indexed-vs-linear result, a couple of inaccuracies in `main.tex`, and unaddressed optional questions (anticipation, failure recovery). Closing P0 + P1 should put the submission at full coverage.
