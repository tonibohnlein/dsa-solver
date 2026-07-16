# Solver framework design

## Boundary

`DsaProblem` is the solver-independent boundary. Solvers return `DsaResult`
objects and never depend on PyPTO IR or MiniMalloc types. Every result is
validated and its objective recomputed independently.

The dependency is one-way: PyPTO may link this library during experimentation
or port a selected heuristic into the compiler. The PyPTO adapter remains
responsible for IR analysis, problem export, result validation, and offset
writeback. The full PyPTO problem is defined in
[`pypto_dsa.md`](pypto_dsa.md).

## Profiles and capabilities

The framework keeps distinct claims separate:

- `standard_dsa`: the single-pool common subset used for direct algorithm
  comparison;
- `pypto_hard_v1`: the current device-correct PyPTO contract;
- `pypto_research_v1`: hard-v1 plus explicitly experimental fields;
- `pypto_structured`: a readable legacy profile; and
- `pypto_core_relaxation`: a named standard lower bound derived from one PyPTO
  pool.

`SolverCapabilities` distinguishes unsupported hard structure from unsupported
objective terms. Hard incompatibility returns `kUnsupported`. An objective-only
mismatch may be reported as an explicit ablation, but no feature is silently
removed. The serialization contract is documented in
[`structured_problem_schema_v1.md`](structured_problem_schema_v1.md).

## Placement and solvers

The shared placement engine accepts a priority order and places buffers at the
lowest aligned address not blocked by temporal conflicts, reservations,
separations, or other enabled hard constraints. This decoder supports:

- deterministic decreasing-size first fit;
- generic seeded local search with swap, insertion, and reversal moves;
- the frozen TVM-style graph-guided ordering search; and
- the experimental PyPTO structured neighborhoods.

The OpenXLA baseline has its own decreasing-size, smallest-fitting-free-chunk
policy and deliberately narrower capabilities. MiniMalloc is the optional exact
baseline for compatible standard instances. The named literature baselines are
documented in [`xla_heap.md`](xla_heap.md) and
[`tvm_hill_climb.md`](tvm_hill_climb.md).

## Benchmark contract

`dsa-suite` records one JSONL row per run and derives CSV and Markdown reports
from those immutable rows. Seeded methods repeat; deterministic methods may be
timed repeatedly. Every row records compatibility, validation, objective
components, budget, and runtime.

Capacity-free standard reports compare achieved peak and runtime. A PyPTO core
relaxation is labeled as a lower bound and never as a compiler-valid placement.
The checked-in standard report is described in
[`benchmarks/results/README.md`](../benchmarks/results/README.md).

## PyPTO adapter pipeline

The intended compiler path is:

```text
InitMemRef
  -> materialize mandatory semantic aliases and writebacks
  -> collect the unmerged fixed-schedule problem
  -> solve and independently validate
  -> write offsets back to MemRefs
```

Pipeline-depth shedding is not hidden inside placement. The adapter first
solves with full pipeline-stage separation. If that search finds no
capacity-fitting placement, it may call `BuildPipelineIntentRelaxation` and
re-solve with typed reuse costs while emitting a performance warning. Joint
schedule/allocation search remains a deferred outer loop that regenerates a
complete DSA problem for each legal schedule.
