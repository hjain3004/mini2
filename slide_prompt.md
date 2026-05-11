# Claude Design Prompt — CMPE 275 Mini 2 Single-Slide PPT

## Goal

Generate **one** highly visual, presentation-grade 16:9 slide for a graduate
CS distributed-systems demo. The audience is the professor and TAs. Minimize
prose; maximize graphs, data tiles, and the architecture diagram. The slide
should communicate **what was built, how the constraints were honored, and
the measured performance wins** at a glance.

## Title

> **Mini 2 — A Distributed Chunked Query Engine over a gRPC Tree Overlay**
> CMPE 275 · Spring 2026 · Himanshu Jain (San José State University)

## Tone & Style

- **Tech-forward, dense-but-clean.** Think Stripe / Linear / Vercel dashboards.
- **Palette:** off-white background (`#fafafa`), ink (`#0f172a`) for text, accent blues (`#3b82f6`), emerald (`#10b981`), amber (`#f59e0b`), violet (`#8b5cf6`), rose (`#ef4444`).
- **Typography:** Inter or DejaVu Sans. Bold for numbers, regular for labels. Avoid serifs.
- **No emojis, no decorative icons.** This is a technical paper, not a marketing deck.
- **Whitespace:** generous; let each region breathe.

## Layout — 16:9, divided into 4 horizontal bands

```
┌──────────────────────────────────────────────────────────────────────────────┐
│ BAND 1 (8% height) — Title bar                                               │
│   left: bold title (one line)   right: subtitle (course / author / date)     │
├──────────────────────────────────────────────────────────────────────────────┤
│ BAND 2 (22% height) — Headline KPI strip                                     │
│   Use the pre-rendered image: report/figures/headline_numbers.png            │
│   (5 colored tiles: 4.1M / 31% / 83% / 3.07× / 20%)                          │
├──────────────────────────────────────────────────────────────────────────────┤
│ BAND 3 (50% height) — TWO COLUMNS                                            │
│  ┌──────────────────────────────────────┬─────────────────────────────────┐  │
│  │ LEFT 55%: Architecture                │ RIGHT 45%: Spec Compliance &   │  │
│  │   topology.png (tree overlay,         │   What We Built                │  │
│  │   2-machine split, A=gateway,         │   (bullet block, see content)  │  │
│  │   Python I leaf, cycle edges dashed)  │                                 │  │
│  └──────────────────────────────────────┴─────────────────────────────────┘  │
├──────────────────────────────────────────────────────────────────────────────┤
│ BAND 4 (20% height) — THREE CHART COLUMNS (equal width)                      │
│   col 1: fairness_compact.png  (Round-Robin vs Greedy bar chart)             │
│   col 2: cache_cold_vs_warm.png (LRU cache cold vs warm)                     │
│   col 3: linear_vs_indexed.png  (selective predicate indexed vs linear)      │
└──────────────────────────────────────────────────────────────────────────────┘
```

## Band 1 — Title bar

- Left, large bold (≈36 pt): `Mini 2 — A Distributed Chunked Query Engine over a gRPC Tree Overlay`
- Right, smaller muted (≈12 pt, color `#64748b`): `CMPE 275 · Spring 2026 · Himanshu Jain · SJSU`
- Thin horizontal rule under the band (1 px, `#e2e8f0`).

## Band 2 — Headline KPI strip

- Embed `report/figures/headline_numbers.png` as a full-width image, no
  cropping, no borders. (It is already styled as 5 rounded tiles with the
  correct palette and big numbers.)
- The five numbers are: **4.1 M** records / 2.7s · **31 %** faster small
  queries · **83 %** fewer chunk pushes · **3.07×** first-chunk speedup ·
  **20 %** speedup on repeat. Do not re-style them.

## Band 3 — Architecture + Spec Compliance

### Left column (55% width): Architecture diagram

- Embed `report/figures/topology.png`.
- Caption directly under it (one line, 10 pt, italic, color `#475569`):
  *9-process gRPC overlay across 2 MacBooks. Node A is the only client-facing gateway; Node I is Python; cycle edges (E–D, E–G) are blocked by* `visited_nodes`.

### Right column (45% width): "Built & Verified"

Render as a compact two-column compliance grid. Each row is a one-line
spec requirement followed by the implementation evidence in a smaller
muted font. Use a subtle checkmark glyph (`✓`, color `#10b981`, before
each requirement; NOT an emoji).

```
✓ Tree overlay, no flat fan-out
   AB, BC, BD, BE, EF, ED, EG, AH, AG, AI · visited_nodes cycle guard
✓ gRPC unary only (no streaming)
   7 RPCs, client polls FetchChunk · manual ChunkManager
✓ Polyglot — C++ servers + Python leaf I
   Same proto contract across both runtimes
✓ Only A talks to the client
   SubmitQuery / FetchChunk / CancelQuery accepted only on A
✓ Typed records (no strings-for-everything)
   int64, double, uint32; enums via uint32
✓ No hardcoded settings
   config/tree/*.conf  and  config/tree-lan/*.conf  (key=value)
✓ Fairness — Round-Robin scheduler
   Small queries finish 31% faster vs greedy
✓ Adaptive chunk sizing
   latency-driven; 83% fewer pushes
✓ Cancellation + abandonment recovery
   PeerCancel wave + TTL janitor (500 ms)
✓ Failure recovery (per-child timeout)
   force-completes hung subtree, returns partial=true
✓ Request anticipation (LRU result cache)
   Serialized QueryFilter key, shared_ptr replay
```

Render the requirement line in `#0f172a`, regular weight; the evidence
line in `#475569`, smaller (≈9 pt). Tight vertical rhythm — 11 rows
should fit comfortably in this column. If needed, drop the two least
critical rows to fit (suggested: keep all eleven; reduce font if tight).

## Band 4 — Three charts (equal columns)

For each chart, use the pre-rendered PNG; no additional captions, the
chart titles inside each PNG are already authoritative.

- Column 1: `report/figures/fairness_compact.png`
- Column 2: `report/figures/cache_cold_vs_warm.png`
- Column 3: `report/figures/linear_vs_indexed.png`

Charts should be the same height, aligned bottom. Leave 12–16 px gutter
between columns.

## Footer (optional, under Band 4, 4% height)

Single muted line, 8 pt, color `#94a3b8`, right-aligned:
`Dataset: NYC 311 Service Requests (12.7 GB, ~33M records, 9 hash-sharded partitions). All measurements: 2-MacBook LAN demo.`

## Constraints / Do-not-do list

- **Do NOT** add stock photography, gradients, drop shadows, or skeuomorphic UI elements.
- **Do NOT** add additional charts beyond the 8 referenced images.
- **Do NOT** restyle the embedded PNGs (no recoloring, no recropping).
- **Do NOT** add "Thank You" / "Q & A" / contact slides — this is a single slide.
- **Do NOT** use emojis. The only inline glyphs are the green checkmarks.
- **Do** keep total slide content readable from 10 ft away (so KPI numbers and node labels must remain large).
- **Do** export at 1920 × 1080 minimum.

## Images to upload alongside this prompt

1. `report/figures/headline_numbers.png`
2. `report/figures/topology.png`
3. `report/figures/fairness_compact.png`
4. `report/figures/cache_cold_vs_warm.png`
5. `report/figures/linear_vs_indexed.png`

## One-sentence elevator summary (for Claude Design to internalize the brief)

A single-slide compliance + results dashboard for a 9-node, 2-machine
distributed gRPC query engine: prove the spec was honored on the left,
prove it's fast on the right, with all numbers backed by reproducible
experiments on a 12.7 GB NYC 311 dataset.
