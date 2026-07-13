# Design notes

## Solver-independent contract

`DsaProblem` is the durable boundary. Solvers consume it and return `DsaResult`; they do not depend on
PyPTO IR or MiniMalloc types. `SolverCapabilities` prevents a client from assuming unsupported structure
was honored.

The portable core is:

- buffers with sizes, alignments, fixed pools, and one or more half-open lifetime intervals;
- address non-overlap for buffers whose live sets intersect;
- peak minimization or fit-under-capacity.

Compiler extensions are explicit constraints or costs:

- colocations encode semantic must-alias;
- separations encode pipeline double buffering, hardware hazards, and operation-specific no-alias rules;
- temporal exclusions encode branch/phi pairs that cannot be live on the same path despite overlapping hulls;
- pins and reserved ranges encode externally owned addresses;
- reuse penalties estimate synchronization induced when lifetime-disjoint buffers share an address;
- bank geometry reserves a future offset-dependent cost model.

## First-fit baseline

Colocation classes become super-buffers. Within every fixed pool, super-buffers are ordered by decreasing
size and placed at the lowest aligned address not blocked by:

- reserved ranges;
- already placed buffers with overlapping live sets;
- explicit separation edges;
- whole-program-exclusive pinned ranges.

The placement engine accepts an explicit priority order. This makes first-fit both a standalone baseline
and the deterministic evaluator used by ordering-based local search.

## Local-search baseline

The first local-search solver explores permutations using swap, insertion, and range-reversal moves. It
uses deterministic seeded restarts and perturbation after stagnation. The best first-fit result remains
the floor, so search cannot return a worse solution than the baseline.

Two lexicographic objectives are available:

- `kMinimizePeak`: total peak, maximum per-pool peak, reuse cost;
- `kFitThenMinimizeReuseCost`: capacity overflow, reuse cost, then peak.

This version searches orderings, similar in scope to existing compiler hill-climbers. The research target
is a placement-aware large-neighborhood search that can move address regions directly, repair conflicts,
and backtrack locally rather than expressing every candidate through a global ordering.

## PyPTO adapter boundary

The future adapter should:

1. materialize semantic aliases before collection;
2. emit original, unmerged allocations as buffers;
3. convert inclusive PyPTO lifetime endpoints to half-open endpoints;
4. emit pipeline, hazard, and operation-semantic separations;
5. emit exact per-space capacity, reserved ranges, and alignment;
6. preserve view-relative offsets during write-back;
7. validate every solver result independently before applying it.

Capacity-driven pipeline-depth shedding remains an adapter/solver coordination problem. It must be
represented explicitly rather than hidden in a pre-solver greedy merge.
