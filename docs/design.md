# Design notes

## Solver-independent contract

`DsaProblem` is the durable boundary. Solvers consume it and return `DsaResult`; they do not depend on
PyPTO IR or MiniMalloc types. `SolverCapabilities` prevents a client from assuming unsupported structure
was honored.

The dependency direction is intentionally one-way: this repository never links PyPTO. PyPTO may link a
stable solver API during experimentation or port a selected heuristic into the compiler. In either case,
the PyPTO adapter owns IR analysis and write-back; schema-v1 structured JSON makes the exact adapter
output replayable without either compiler.

The portable core is:

- buffers with sizes, alignments, fixed pools, and one or more half-open lifetime intervals;
- address non-overlap for buffers whose live sets intersect;
- peak minimization or fit-under-capacity.

Schema v1 stores a bound program/architecture pair. The proposed corpus layer
separates lowered program structure from versioned architecture resources and
binds them before invoking a solver; see
[`architecture_binding.md`](architecture_binding.md). This distinction is
necessary because target-dependent lowering can change the DSA graph itself,
not just its capacities.

Compiler extensions are explicit constraints or costs:

- colocations encode semantic must-alias;
- separations encode pipeline double buffering, hardware hazards, and operation-specific no-alias rules;
- temporal exclusions encode branch/phi pairs that cannot be live on the same path despite overlapping hulls;
- pins and reserved ranges encode externally owned addresses;
- reuse penalties estimate synchronization induced when lifetime-disjoint buffers share an address;
- bank geometry reserves a future offset-dependent cost model.

The optional `PyptoStructure` retains normalized alias-class members and capacity-gated pipeline
group/stage/residue membership. It is provenance for structured neighborhoods, not an implicit constraint:
the generic fields above remain the complete feasibility and objective contract.

## Benchmark profiles

One runner serves three claims that must remain distinct:

- `standard_dsa`: the MiniMalloc-compatible common subset used for direct exact-solver comparisons;
- `pypto_hard_v1`: device-correct production constraints only;
- `pypto_research_v1`: the same PyPTO base contract plus explicitly experimental fields;
- `pypto_structured`: readable legacy profile that predates the hard/research split;
- `pypto_core_relaxation`: a named, per-pool standard relaxation used only as a lower bound.

The relaxation is generated rather than hand-authored. Its envelope records the source instance, source
pool, and every removed feature. Schema v1 refuses temporal exclusions, colocations, overlapping
intervals within one buffer, and flexible pool assignment because converting any of them to independent
interval-only MiniMalloc rows can accidentally strengthen the problem and invalidate the lower-bound
claim.

## Capability and objective contracts

Compatibility has two axes. A hard-feature mismatch means a solver cannot produce a valid solution and
returns `kUnsupported`. An objective mismatch means the placement may remain structurally valid but the
solver does not optimize all requested metrics. This allows first-fit to be reported honestly as a
baseline without pretending it is reuse-cost-aware.

Objectives are lexicographic vectors in schema v1. Supported raw components are capacity overflow, total
and maximum peak, reuse cost, and bank cost. Keeping components separate avoids assigning arbitrary
weights between bytes and compiler performance proxies. A future weighted or Pareto mode requires an
explicit schema evolution.

## Suite and report contract

`dsa-suite` executes first-fit once and seeded search methods repeatedly. It records one JSONL object per
run, then derives long-form CSV and Markdown from those records. Runtime aggregation uses the median;
solution selection uses the instance's lexicographic objective. Every returned placement is validated and
its objective is recomputed independently of the solver.

Raw validity has two levels. `placement_valid` checks address geometry, relaxing capacity only for an
explicit best-effort diagnostic placement. `solution_valid` checks every original hard constraint,
including pool capacity.

The pinned MiniMalloc core is an optional in-process exact baseline. Standard instances compare directly.
For a PyPTO profile, the runner first invokes the schema-defined per-pool relaxation and labels exact
results `core_lower_bound`. Exact runs that exhaust their budget remain timeouts even when they produced a
feasible upper bound, so an incomplete search can never create a false optimality-gap or lower-bound
claim.

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
uses deterministic seeded restarts and perturbation after stagnation. Restart initialization, ordinary
moves, and repair decodes share one global decoder-evaluation budget (including initialization), and
every decoded placement is compared with the global best. The best first-fit result remains the floor,
so search cannot return a worse solution than the baseline.

Two lexicographic objectives are available:

- peak: total peak, then maximum per-pool peak;
- fit-cost: capacity overflow, reuse cost, total peak, then maximum peak.

This version searches orderings, similar in scope to existing compiler hill-climbers. The research target
is a placement-aware large-neighborhood search that can move address regions directly, repair conflicts,
and backtrack locally rather than expressing every candidate through a global ordering.

## OpenXLA heap baseline

`XlaHeapSolver` freezes OpenXLA's spatial decreasing-size/best-fit policy for
the standard core. It sorts by size and lifetime duration, then selects the
smallest aligned free chunk that fits. Capability matching prevents the method
from silently consuming PyPTO-only structure; structured instances reach it
only through named core relaxations. See [`xla_heap.md`](xla_heap.md).

## TVM hill-climb baseline

`TvmHillClimbSolver` preserves Apache TVM USMP's recognizable search policy as a distinct literature
baseline. It starts from decreasing size/conflict degree and repeatedly swaps earlier first- or
second-level conflict neighbors of a buffer touching a pool's high-water mark. A rapidly decaying
acceptance rule permits occasional worse-peak moves.

The policy uses the same generalized placement engine as the other built-in solvers. Its search graph
therefore collapses colocations and adds temporal conflicts, separations, and whole-program-exclusive
pins as blocking edges. The decoder continues to honor fixed pools, multi-interval liveness, alignment,
reserved ranges, and pinned offsets; candidate comparison uses the requested lexicographic objective.
This makes standard-profile runs comparable with TVM's algorithmic idea while allowing the identical
policy to run as an explicit ablation on PyPTO instances.

This is a behavioral reimplementation, not bit-for-bit compatibility: it uses a portable seeded random
engine, preserves the best solution seen, uses the current buffer's alignment through the shared decoder,
and can navigate complete over-capacity placements. See [the detailed study](tvm_hill_climb.md).

## PyPTO-structured search

`PyptoStructuredSearchSolver` uses the full compatible placement engine and
the schema objective, but changes the neighborhood units. Half of its moves
remain generic swap/insertion/reversal moves; the other half relocate pipeline
groups as blocks, reprioritize multi-member semantic alias identities, or move
endpoints of sparse reuse-penalty edges. Candidate comparison is lexicographic,
normally capacity overflow, reuse cost, total peak, and maximum peak.

This is the first explicit structured-search baseline, not the final proposed
algorithm. Alias and pipeline metadata are provenance for moves; generic hard
constraints remain authoritative. Future direct gap moves and bounded local
repair should be introduced as separate ablatable neighborhoods.

## PyPTO adapter boundary

The adapter now:

1. materialize semantic aliases before collection;
2. emit original, unmerged allocations as buffers;
3. convert inclusive PyPTO lifetime endpoints to half-open endpoints;
4. emit pipeline, hazard, and operation-semantic separations with typed provenance;
5. emit exact per-space capacity, reserved ranges, and alignment;
6. preserve view-relative offsets during write-back;
7. retain normalized alias and pipeline groups plus sparse adjacent cross-stage reuse costs;
8. validate every solver result independently before applying it.

Capacity-driven pipeline-depth shedding remains an adapter/solver coordination problem. It must be
represented explicitly rather than hidden in a pre-solver greedy merge.

The first implementation can keep that decision in an adapter-controlled solve/shed/re-solve loop. A
research extension can promote pipeline groups and buffering depth into solver decision variables, with
depth loss exposed as another objective component.
