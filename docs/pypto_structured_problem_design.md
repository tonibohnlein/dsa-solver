# PyPTO structured DSA design proposal

## Decision summary

The PyPTO benchmark should expose two related but non-interchangeable problems
from every fixed compiler schedule:

1. a **standard DSA projection** for direct comparison with MiniMalloc, XLA,
   TVM, and generic local search; and
2. a **PyPTO structured problem** whose solution is legal to write back into the
   compiler.

The standard projection is a lower-bound experiment. It may remove PyPTO
constraints, but must record every removed feature and must never be presented
as a valid compiler placement. The structured problem is the production
contract and fails closed when a solver lacks a required capability.

This proposal keeps scheduling fixed. Joint scheduling/allocation is a later
outer problem whose candidate schedules each produce one structured DSA input.

## Layered model

### Layer 0: schedule and event order

The input includes a stable schedule identity and the ordered allocation events
used to derive lifetimes. Schema v1 uses PyPTO read-before-write sub-points and
one conservative physical allocation hull. A benchmark result is meaningful
only for that schedule and exporter revision.

The DSA solver does not reorder operations. A future scheduling search calls the
allocator as an inner oracle rather than placing schedule choices inside buffer
records.

### Layer 1: physical allocation identities

The exporter runs mandatory semantic-alias materialization first. Views,
in-place results, branch-phi storage, and loop carries that must denote one
physical allocation become one buffer identity with:

- maximum required allocation extent;
- one fixed memory pool;
- one conservative lifetime hull; and
- provenance listing the logical values represented by the identity.

Alias classes are therefore explanatory data in v1, not additional equality
constraints. A future explicit PTO-buffer-handle IR may become the source of
these identities, but the benchmark contract must not depend on that migration.

### Layer 2: hard placement constraints

For buffer `i`, let its fixed pool, size, alignment, lifetime, and address be
`p_i`, `s_i`, `a_i`, `L_i`, and `x_i`. Let
`A_i = [x_i, x_i+s_i)`. A valid placement satisfies the mathematical
`pypto_hard_v1` contract in [`pypto_hard_v1.md`](pypto_hard_v1.md):

- alignment, pool capacity, and reserved-range constraints;
- spatial disjointness whenever physical lifetimes conflict;
- unconditional disjointness for typed separation edges; and
- `whole_slot_v1`: equal base or completely disjoint ranges for every pair in a
  pool.

Typed separations carry one of these proven reasons:

- simultaneous pipeline stage/residue occupancy;
- target operation hazard; or
- semantic no-alias requirement.

These are feasibility constraints. No objective may trade them away.

### Layer 3: provenance for search and analysis

The following fields describe structure but do not independently change
feasibility:

- logical members of an already-collapsed alias identity;
- pipeline group, stage, residue, requested depth, and effective depth;
- target, source path, compiler revision, and schedule fingerprint.

A structured heuristic may use this information to construct moves. It must
still pass the same independent hard-constraint validator as a generic solver.
If a provenance field occurs in no unique corpus instances, it cannot motivate
a claimed algorithmic improvement.

### Layer 4: experimental preferences

Performance preferences belong only to `pypto_research_v1` until calibrated.
Candidate descriptors include:

- address reuse across pipeline stages or pipe categories;
- pipeline depth lost to capacity;
- emitted event/wait/barrier changes;
- bank or address-residue effects; and
- placement headroom per pool.

The schema may record raw descriptors, but an unmeasured unit cost is not a
production objective. Research reports must show each component separately.

## Objective design

### Production hard-v1 baseline

Compilation first requires zero capacity overflow. Among feasible placements,
the current reproducible baseline minimizes:

```text
(sum of pool high-water marks, maximum pool high-water mark)
```

This is intentionally performance-neutral. It is appropriate for validating
placement algorithms and produces the same ordering as peak minimization on a
single-pool standard instance.

For a future multi-pool objective, raw byte sums should not silently equate one
byte in unrelated memories. A better capacity-headroom tie-break is:

```text
sorted_descending(H_p / C_p), then sum_p H_p
```

where every relevant capacity `C_p` is known. This remains experimental until
the benchmark reports whether it changes real compiler choices beneficially.

### Performance-aware research objective

Do not collapse bytes, synchronization, and pipeline depth into one arbitrary
scalar. Evaluate feasible candidates with this ordered decision process:

1. reject any hard-constraint or capacity violation;
2. compare a measured/categorical performance-risk vector, if validated;
3. compare normalized pool pressure;
4. use total and maximum peak as deterministic ties; and
5. use solver runtime only as a benchmark metric, never a placement objective.

If measurements reveal genuine trade-offs, report a Pareto frontier rather
than hiding them behind weights. A production scalarization requires held-out
DeepSeek and Qwen evidence.

## Standard DSA projection

For each fixed pool, the suite generates a `pypto_core_relaxation`:

- retain size and one interval fragment per standard buffer;
- map the pool to one standard arena;
- remove PyPTO whole-slot structure, separations, reservations, pins, banks,
  costs, and cross-pool coupling;
- normalize alignment when required by the exact baseline; and
- record every removed feature in `relaxed_features`.

Removing constraints enlarges the feasible set, so a certified optimum is a
lower bound. Projection is refused when schema v1 cannot prove that the result
is a relaxation, including flexible pool assignment, colocations, or temporal
exclusions.

The report must therefore contain two tables:

- **Standard DSA:** public instances plus compiler-derived pool relaxations,
  with MiniMalloc, first fit, XLA heap, TVM hill climb, and local search.
- **PyPTO structured DSA:** full compiler instances, with every compatible
  heuristic, independent validity, peak, and experimental cost components.

## Features deliberately outside hard-v1

| Candidate feature | Required evidence before promotion |
| --- | --- |
| reusable lifetime holes | physical value-death proof across control flow plus model regression |
| partial slot subdivision | explicit downstream dependency/slice representation and device correctness |
| flexible pool assignment | codegen support and a joint memory-space selection pass |
| pinned named buffers | real compiler case not representable as a reserved range |
| branch temporal exclusion | independently verified mutual-exclusion proof |
| piecewise size/resizing | value/slice-to-allocation representation with valid writeback semantics |
| bank/reuse/depth costs | controlled device correlation and held-out prediction |
| schedule decisions | bounded outer schedule search with legality and regenerated lifetimes |

## Solver capability contract

Every solver declares supported hard features and objective terms. The suite:

1. validates the input profile;
2. checks capabilities before interpreting a result;
3. may still run a structurally compatible heuristic as an explicitly labeled
   ablation;
4. independently validates every placement; and
5. never fills an unsupported table cell with a result from a relaxed problem.

`pypto_structured_search` is not a new problem definition. It is one heuristic
for the structured problem and must be compared with generic heuristics on the
same documents.

## Evaluation and promotion gates

A proposed PyPTO-specific constraint, objective, or move is accepted only when:

1. its producer semantics are written formally;
2. the fixed-revision corpus reports nonzero unique occurrence;
3. an independent validator enforces any hard effect;
4. standard projections remain explicitly labeled lower bounds;
5. all compatible solvers run with disclosed budgets and seeds;
6. compiler and device numerical regression remain green; and
7. performance preferences predict held-out device results.

Until these gates pass, improve generic DSA search on the standard table and
retain PyPTO-specific fields as provenance or research-only data.
