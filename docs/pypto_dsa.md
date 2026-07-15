# PyPTO DSA problem

This document defines the PyPTO-specific Dynamic Storage Allocation problem,
its relationship to standard DSA, and the evidence required before adding new
constraints or objectives. It is the authoritative design document for the
`pypto_hard_v1` and `pypto_research_v1` profiles.

## Scope

For one fixed compiler schedule, PyPTO exports two related problems:

1. the full PyPTO problem, whose solution must be safe to write back into the
   compiler; and
2. independent per-pool standard-DSA projections used to compare algorithms
   with MiniMalloc and other literature baselines.

The projection may remove constraints and therefore provides only a lower
bound. It is never a valid compiler placement. Scheduling is fixed in both
problems; schedule/allocation co-optimization is a later outer problem.

## Current hard-v1 contract

Let `B` be physical allocation identities and `P` fixed local-memory pools. For
each buffer `i in B`, the exporter provides:

- size `s_i > 0` and alignment `a_i >= 1`;
- one fixed pool `p_i in P`; and
- one conservative half-open lifetime `L_i = [l_i, u_i)`.

Each pool has an optional capacity `C_p` and half-open reserved address ranges
`R_p`. `S` is the set of unconditional separation pairs. The solver chooses
the byte offset `x_i`, giving address range `A_i = [x_i, x_i + s_i)`. Define:

```text
T(i,j) := p_i = p_j and l_i < u_j and l_j < u_i.
```

A placement is valid exactly when:

```text
x_i >= 0
x_i mod a_i = 0
x_i + s_i <= C_p_i                         when C_p_i exists
A_i intersect r = empty                    for every r in R_p_i
T(i,j) => A_i intersect A_j = empty        for every i != j
(i,j) in S => A_i intersect A_j = empty
p_i = p_j => (x_i = x_j or A_i intersect A_j = empty)
```

The last condition is the current `whole_slot_v1` reuse contract. Two
lifetime-disjoint buffers may use the same base, with a slot large enough for
the larger buffer, or occupy disjoint slots. Partial overlap at different bases
is forbidden because PyPTO's downstream dependency representation does not yet
model arbitrary sub-slot tenancy safely.

Mandatory aliases are materialized before export. Views, in-place results,
branch-phi storage, and loop carries that denote one allocation become one
buffer identity with the maximum required extent. Alias-class members in the
JSON are provenance for that collapse, not additional equality constraints.

For each pool:

```text
H_p = max({r.end | r in R_p} union {x_i + s_i | p_i = p})
```

Among feasible placements, hard-v1 minimizes lexicographically:

```text
(sum_p H_p, max_p H_p)
```

Capacity feasibility is mandatory. Peak minimization supplies deterministic
headroom; it is not a device-performance model.

## Lifetime semantics

PyPTO statement points are expanded into read and write sub-points. A buffer
defined at statement `d` starts at `2*d+1`; a final read at statement `u` ends
at `2*u+1`:

```text
[2*d + 1, max(2*d + 2, 2*u + 1))
```

This permits read-before-write reuse at one statement boundary. Each physical
allocation has one conservative `definition..last-use` hull. A union of
individual SSA-member ranges is not a physical-liveness proof.

This distinction is device-critical. A previous exporter produced a false
lifetime hole in DeepSeek-v4 softmax-pool loop-carried accumulators. The solver
legally filled the declared hole and corrupted a value read later. Collapsing
the allocation to its physical lifetime hull fixed all four deterministic
prefill regressions without increasing peak memory.

## PyPTO structure

The structured problem has four semantic layers:

| Layer | Contents | Effect |
| --- | --- | --- |
| schedule | schedule identity and ordered lifetime events | fixes this DSA instance |
| physical identity | materialized aliases and allocation extent | defines solver buffers |
| hard placement | lifetimes, pools, capacity, alignment, reservations, whole-slot reuse, separations | determines feasibility |
| provenance | logical alias members, pipeline group/stage/residue/depth, target and source revision | supports audit and search moves |

Typed separations currently represent simultaneous pipeline copies, target
operation hazards, or semantic no-alias requirements. Their reason is
provenance; every separation is an unconditional hard constraint.

The exporter and independent validator, not a heuristic, define correctness.
The schema and capability checker must reject a solver that cannot enforce a
required hard feature.

## Feature status

| Feature | Status | Motivation or required evidence |
| --- | --- | --- |
| physical buffer identity | hard | writeback must preserve mandatory aliasing |
| size, alignment, fixed pool | hard | allocation and target instruction safety |
| one lifetime hull | hard | device-proven physical-liveness requirement |
| capacity and reservations | hard | compiler/runtime memory ownership |
| whole-slot reuse | hard | arbitrary partial overlap caused device-unsafe dependency behavior |
| typed separations | hard | pipeline, target-hazard, and semantic no-alias safety |
| alias and pipeline groups | provenance | audit structure and define ablatable search moves |
| target, schedule, producer metadata | provenance | reproducibility |
| peak objective | hard-v1 objective | capacity headroom, not runtime performance |
| multi-interval lifetime | experimental | requires a physical value-death proof and model regression |
| colocations | experimental | mandatory aliases are currently collapsed before export |
| temporal exclusions | experimental | requires an independently checked control-flow proof |
| pinned named buffers | experimental | current cases use pool reservations |
| flexible pool assignment | experimental | requires joint memory-space selection and codegen support |
| partial slot subdivision | experimental | requires slice-aware downstream dependency semantics |
| piecewise size or resizing | experimental | requires explicit value/slice-to-allocation representation |
| bank, reuse, or depth cost | experimental | requires controlled device calibration and held-out prediction |

`pypto_hard_v1` accepts only the current hard and provenance rows.
`pypto_research_v1` retains the same input-stage, lifetime-ordering, and
whole-slot contracts but may carry explicitly experimental fields. Research
fields never become production requirements merely because the schema can
represent them.

## Standard DSA projection

Each fixed pool can be projected into a capacity-free `standard_dsa` instance:

- keep buffer sizes and sound lifetime intervals;
- map the pool to one arena;
- remove alignment, reservations, PyPTO structure, separations, pins, banks,
  costs, and cross-pool context; and
- record the source pool and every removed feature.

Removing constraints enlarges the feasible set, so a certified exact optimum
is a lower bound on the source pool. Projection is refused if transformation
could strengthen the problem, including unsupported flexible pool assignment,
colocations, temporal exclusions, or overlapping intervals from one physical
buffer.

Current published tables intentionally report only this standard comparison:
MiniMalloc exact, first fit, XLA heap, TVM hill climb, and generic local search
over the public MiniMalloc set and compiler-derived projections. Structured
PyPTO results should be added only after the variant and its objective are
stable.

## Objective research

The first production objective remains peak-only. Once a placement fits,
however, additional headroom may be less valuable than avoiding dependencies
that serialize execution or retaining pipeline depth. Candidate research
descriptors include:

- normalized pressure `H_p / C_p` for each pool;
- address reuse across pipeline stages or pipe categories;
- effective versus requested pipeline depth;
- emitted event, wait, barrier, or dependency changes; and
- bank or address-residue effects.

Do not combine bytes, synchronization, and depth with arbitrary weights.
Report raw components and Pareto choices until device measurements support a
categorical, lexicographic, or weighted model.

Three controlled experiments provide that evidence:

1. **Pairwise reuse:** compare disjoint slots with equal-base reuse while all
   other placements and generated computation remain fixed.
2. **Pipeline depth:** force each feasible effective depth without changing
   tiling or operation order, then measure peak, latency, utilization, and
   synchronization.
3. **Capacity frontier:** sweep each pool from minimum fit to device capacity
   and record reuse, depth shedding, generated synchronization, and latency.

Every variant must pass independent placement and numerical validation.
Measurements require pinned revisions, randomized repeated timing, device
health checks, preserved PTO, and held-out DeepSeek and Qwen cases. A feature is
promoted only when it improves held-out placement selection, not merely
training-set correlation.

## Structured search

`pypto_structured_search` is a heuristic for this problem, not a separate
problem definition. It currently combines generic swap/insertion/reversal moves
with pipeline-block, alias-identity, and reuse-edge neighborhoods. All
candidates use the common placement engine and validator.

The present implementation is an ablation baseline. The next solver work is:

1. measure which structured features occur in unique corpus instances;
2. independently disable and compare each move family;
3. add direct offset-to-gap moves;
4. add bounded local repair or ejection chains; and
5. introduce cost-aware moves only after the corresponding cost is calibrated.

No move is evidence of PyPTO-specific progress unless its target feature occurs
in the corpus and improves a disclosed metric under the same hard constraints.

## Scheduling and allocation

`CanonicalizeIOOrder` selects a legal topological statement order before
lifetime analysis. That order determines the exported rectangles, but it is
not currently a global allocator-aware schedule optimizer. A practical future
formulation is a bounded outer loop:

```text
schedule candidate
  -> regenerate lifetimes and constraints
  -> bounded DSA solve and validation
  -> placement and measured-risk estimate
  -> accept or propose another legal schedule
```

This preserves the fixed-schedule allocator as an independently testable inner
solver. Device performance, rather than peak alone, must determine the eventual
co-optimization objective.

## Promotion gate

A new hard constraint, objective term, or structured move is accepted only
when:

1. its compiler semantics are stated formally;
2. a fixed-revision, deduplicated corpus reports nonzero occurrence;
3. an independent validator enforces every hard effect;
4. standard projections remain explicitly labeled lower bounds;
5. compatible solvers run with disclosed budgets and seeds;
6. compiler and device numerical regression remain green; and
7. performance preferences predict held-out device results.

The JSON representation and profile validation are specified in
[`structured_problem_schema_v1.md`](structured_problem_schema_v1.md). Program
and target identity are specified in
[`architecture_binding.md`](architecture_binding.md).
