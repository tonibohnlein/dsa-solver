# Structured problem schema v1

Schema v1 is the replay boundary between compiler adapters and the standalone
solver. The C++ envelope is `dsa::StructuredProblemDocument`; the machine
contract is [`schemas/dsa-problem-v1.schema.json`](../schemas/dsa-problem-v1.schema.json).

```text
compiler IR -> adapter -> schema-v1 JSON -> solver -> independent validator
```

## Envelope

```json
{
  "schema_version": 1,
  "profile": "pypto_hard_v1",
  "instance": "kernel_name",
  "metadata": {"target": "ascend910b"},
  "problem": {}
}
```

`profile` validates which fields a producer may emit. It does not necessarily
name a different optimization problem. In particular, `pypto_hard_v1` captures
standard DSA plus compiler provenance and ordinary hard constraints.

## Problem

Each pool has an optional capacity and reserved address ranges. Each buffer has
a size, alignment, one or more half-open live intervals, and allowed pools.
Built-in compiler use currently fixes each buffer to one pool.

Hard constraints are:

- `colocations`: equal pool and offset;
- `separations`: disjoint address ranges regardless of lifetime;
- `temporal_exclusions`: control-flow pairs proven unable to coexist;
- `pinned_allocations`: fixed pool and offset; and
- lifetime conflicts, reservations, alignment, and capacity.

Lifetime-disjoint buffers may partially overlap at different bases.

## PyPTO provenance

```json
{
  "alias_classes": [
    {"buffer": 7, "members": ["tile", "slice"]}
  ],
  "pipeline_groups": [{
    "group": 4,
    "pool": 3,
    "slot_size": 32768,
    "depth": 3,
    "effective_depth": 2,
    "members": [{"buffer": 7, "stage": 0, "residue": 0}]
  }]
}
```

Alias members describe values already collapsed into one physical buffer.
Pipeline groups record source depth and stage/residue membership. A strict
capture uses `effective_depth == depth` and a distinct residue per stage. Hard
pipeline requirements are represented separately as typed `separations`;
provenance alone does not change feasibility.

A soft fallback retains that source mapping so the document records which
intent was relaxed. `effective_depth` is not recomputed from the resulting
placement; achieved overlap must be reported from the placement and its reuse
costs.

## Optional cost model

`cost_model.reuse_penalties` contains sparse buffer pairs. A cost is charged
when the pair has disjoint lifetimes but overlapping placed address ranges.
Reasons distinguish provenance, not a calibrated hardware cost:

- `pipeline_serialization`: implemented strict-to-soft fallback for pipeline
  stages, grounded by PyPTO PR #1949;
- `cross_pipe`: experimental placeholder for a placement-induced PTOAS
  set/wait; production evidence should record the actual pipe pair and
  counterfactual synchronization delta;
- `load_motion_serialization` and `cross_core`: reserved schema vocabulary with
  no independent producer or calibrated objective today;
- `event_budget`: reserved for a future non-additive model of event
  multiplicity reduction and `PIPE_ALL` fallback; and
- `generic`: test or externally supplied cost with no stronger claim.

The mechanism definitions, examples, and evidence status are documented in
[`pypto_dsa.md`](pypto_dsa.md). Producers must not stack overlapping categories
on one pair until a non-double-counting composition rule exists.

For PyPTO's fixed-capacity formulation, capacity is a hard feasibility
constraint and `reuse_cost` is minimized among fitting placements. Schema v1
can still encode lexicographic vectors using `capacity_overflow`, `total_peak`,
`max_peak`, `reuse_cost`, and `bank_cost`; `capacity_overflow` is useful as an
internal search signal or benchmark diagnostic, not as an accepted
over-capacity solution.

`BuildPipelineIntentRelaxation` is the explicit strict-to-soft transition. It
removes only `pipeline_stage` reasons, preserves every other reason on the
pair, adds `pipeline_serialization` costs for lifetime-disjoint pairs, selects
the fit-then-reuse objective, and marks the document `pypto_research_v1`.
Ordinary solvers never perform this relaxation implicitly.

## Profiles

- `standard_dsa`: literature-compatible single-pool problem.
- `pypto_hard_v1`: standard DSA plus validated compiler constraints and
  provenance.
- `pypto_research_v1`: PyPTO capture with explicit experimental constraints or
  costs.
- `pypto_structured`: readable legacy profile; new producers should not emit
  it.
- `pypto_core_relaxation`: named standard lower-bound projection with removed
  features recorded.

The mathematical distinction is documented in [`pypto_dsa.md`](pypto_dsa.md):
only experimental features that change feasibility or the objective define a
research refinement.

## Capability matching and architecture binding

`CheckSolverCompatibility` reports unsupported hard features and objective
terms; nothing is silently dropped. A core relaxation is a separate document.

An architecture-free capture leaves capacities unset and records
`metadata.lowering_abi`. `dsa-bind` combines it with a versioned architecture
document; see [`architecture_binding.md`](architecture_binding.md).
