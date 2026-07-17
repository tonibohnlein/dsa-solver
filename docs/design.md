# Solver framework design

## Boundary

`DsaProblem` is the solver-independent boundary. It contains buffers, memory
arenas, placement constraints, and an objective. A solver returns `DsaResult`;
the shared validator independently checks the placement and recomputes its
objective.

PyPTO owns compiler analysis and writeback:

```text
PyPTO IR -> adapter -> DsaProblem -> solver -> validator -> MemRef offsets
```

The standalone library never depends on PyPTO IR. During research PyPTO may
link it; after selecting a heuristic, that heuristic can be ported into PyPTO
and the dependency removed.

## Solvers

| Solver | Policy | Role |
| --- | --- | --- |
| `first_fit` | decreasing-size order, lowest valid address | deterministic fallback |
| `xla_heap` | OpenXLA decreasing-size spatial best fit | frozen literature baseline |
| `tvm_hill_climb` | TVM graph-guided ordering swaps | frozen literature baseline |
| `local_search` | seeded swap/insertion/reversal ordering search | generic research baseline |
| `pypto_structured_search` | generic moves plus pipeline, alias, and penalty-guided moves | experimental PyPTO heuristic |
| `minimalloc_exact` | pinned Google MiniMalloc solver | optional exact standard-DSA baseline |

Most heuristics decode a buffer order with the shared placement engine. The XLA
solver retains its own smallest-fitting-free-chunk policy. Details of the named
reimplementations are in [`xla_heap.md`](xla_heap.md) and
[`tvm_hill_climb.md`](tvm_hill_climb.md).

## Capability matching

`SolverCapabilities` separates hard placement support from objective support.
Unsupported required structure returns `kUnsupported`; benchmark projections
are explicit documents and never silent feature removal. Profiles and feature
semantics are defined in
[`structured_problem_schema_v1.md`](structured_problem_schema_v1.md).

## Adapter pipeline

The intended PyPTO path is:

```text
InitMemRef
  -> materialize semantic aliases and writebacks
  -> collect physical buffers and conservative lifetimes
  -> solve and independently validate
  -> write byte offsets to MemRefs
```

The adapter also owns the strict-then-soft pipeline-intent policy. Its
mathematical formulation and evidence are in [`pypto_dsa.md`](pypto_dsa.md).

## Benchmark records

`dsa-suite` writes immutable JSONL rows, then derives CSV and Markdown reports.
Rows include compatibility, status, validation, objective components, search
budget, seed, and runtime. Capacity-free standard reports compare peak and
solver runtime; compiler-specific conclusions require the original structured
problem and independent device evidence.
