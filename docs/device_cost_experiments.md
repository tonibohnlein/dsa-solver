# Device calibration protocol for PyPTO memory costs

## 0. Goal and stop conditions

Calibrate whether address reuse and pipeline-depth changes affect Ascend 910B
runtime, and whether exported features predict that effect. This protocol does
not tune `pypto_structured_search` directly. It produces measured data from
which a cost model may later be proposed.

Stop and classify rather than averaging through any of these conditions:

- either placement fails independent validation or numerical golden checks;
- generated PTO differs in computation, tiling, or scheduling rather than only
  allocation/dependency realization;
- device health changes, a reset occurs, or thermal/frequency state is unstable;
- the requested address relation is optimized away or not visible in PTO;
- a run cannot identify the expected pipeline group and selected buffer pair.

## 1. Pins and environment

Record exact commits for PyPTO, dsa-solver, PyPTO-Lib, runtime, pto-isa, and the
PTOAS release plus verified archive SHA-256. Record CANN, architecture, Python,
device ID, and device health before and after every block. Build from the pinned
worktrees with at most two compile jobs and one codegen worker per process.

Use a fresh environment where the imported `pypto`, `pypto_core`, and
`_task_interface` paths are printed. Do not copy extensions from another
checkout. Preserve every schema document, solution, kernel PTO, log, and timing
row under one immutable artifact root.

## 2. Corpus selection

Run `dsa-suite` first and use `features.csv` to select cases rather than model
names alone:

- at least three kernels with pipeline groups but no depth shedding;
- at least three with `effective_depth < depth`;
- reuse candidates from Vec and cube-side pools;
- same-size equal-base reuse and unequal-size whole-slot reuse;
- controls with no reuse candidate and no pipeline group;
- DeepSeek and Qwen representatives after structural deduplication.

Reject allocation-trivial cases. Record the exact buffer IDs, pool, sizes,
lifetimes, pipeline stages/residues, and baseline addresses before device work.

## 3. Experiment A — pairwise reuse cost

For one lifetime-disjoint pair `(i,j)` in a fixed pool, compile these placement
variants while keeping all other placements stable:

| Variant | Address relation | Purpose |
| --- | --- | --- |
| A0 | distinct disjoint slots | no-reuse control |
| A1 | equal base, whole-slot reuse | measured reuse treatment |
| A2 | equal base for a same-stage/same-pipe-category pair | cheap-reuse control |
| A3 | equal base for a cross-stage pair | tests the current pipeline proxy |

Do not test partial overlap: it is outside hard-v1. Add a hard separation for A0
and a pinned/equality diagnostic override for A1-A3 in an unpushed experiment
worktree; never weaken the validator. Confirm in PTO that only addresses and
the resulting dependency/synchronization instructions differ.

For each variant:

1. compile once and run golden validation;
2. perform 10 untimed warmups;
3. collect at least 100 timed samples in randomized A0/A1/A2/A3 block order;
4. repeat the block at least five times;
5. capture PTOAS event/barrier counts and an L0 per-pipe trace for one median run.

Report median, p05/p95, median absolute deviation, and paired bootstrap confidence
interval for `(variant - A0) / A0`. A reuse penalty is supported only if its sign
is consistent across blocks and the interval excludes the predeclared practical
noise floor.

## 4. Experiment B — pipeline depth cost

For a pipeline group with requested depth `D`, force each feasible effective
depth `d in [1,D]` without changing tiling or operation order. Use the same
placement engine and validate all stage/residue separations.

Measure:

- numerical correctness;
- per-pool high-water and capacity headroom;
- end-to-end and kernel latency distributions;
- MTE/CUBE/Vec busy percentage and visible gaps;
- event, wait, barrier, and dependency-edge counts.

The primary comparison is throughput/latency at each `d`; bytes saved are a
constraint trade-off, not a runtime cost unit. A depth-loss objective is allowed
only if the measured curve is monotone enough to predict held-out kernels. If
the same depth change has opposite effects across kernels, retain the raw group
feature and use a categorical or learned model rather than a universal scalar.

## 5. Experiment C — capacity frontier

For each selected kernel, sweep an artificial per-pool capacity from the
hard-v1 minimum fit to the device capacity. At every breakpoint record:

- which reuse edges appear;
- which pipeline groups shed depth;
- the chosen peak and runtime;
- whether PTOAS synchronization changes.

This separates two questions that a single peak table conflates: “does it fit?”
and “which fitting placement is faster?”. The output is a Pareto frontier over
capacity headroom and measured latency, not a weighted score.

## 6. Analysis and model acceptance

Fit on named training kernels and reserve at least one DeepSeek and one Qwen
representative per feature category as held-out tests. Candidate predictors may
include:

- same/cross pipeline stage and residue;
- pool, core class, sizes, and slot-size ratio;
- lifetime boundary distance;
- requested/effective pipeline depth;
- emitted event/wait/barrier deltas.

The current unit-valued adjacent pipeline penalty is the null hypothesis, not
ground truth. Compare it with: no cost, categorical pair costs, and one-round
PTOAS-feedback features. Accept a model only when it improves held-out rank
correlation and selects a placement whose measured latency is no worse than the
peak-only baseline within the confidence threshold.

Do not scalarize bank, synchronization, depth, and bytes merely because the
schema can store them. First publish the raw measurements and Pareto choices.

## 7. Deliverables

Provide:

1. pin/environment table and clean-tree proof;
2. `features.csv` rows used for case selection;
3. variant manifest mapping every run to problem fingerprint and placement;
4. golden results, raw timing TSV, summary statistics, PTO diffs, and traces;
5. capacity/depth frontier tables;
6. classifications for every failure or skipped case;
7. a recommendation: keep peak-only, add a measured categorical cost, request
   PTOAS feedback, or collect more data.

No production source change belongs in this task. Diagnostic variants must be
committed in separate local branches or preserved as diffs, then the pinned
build must be restored.
