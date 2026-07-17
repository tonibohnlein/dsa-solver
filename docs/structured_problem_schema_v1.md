# Structured problem schema v1

Schema v1 is the replay boundary between compiler adapters and the standalone
solver:

```text
compiler IR -> adapter -> problem JSON -> solver -> solution JSON -> validator
```

The C++ envelopes are `StructuredProblemDocument` and
`StructuredSolutionDocument`. Their JSON schemas are
[`dsa-problem-v1.schema.json`](../schemas/dsa-problem-v1.schema.json) and
[`dsa-solution-v1.schema.json`](../schemas/dsa-solution-v1.schema.json).

## Envelope and replay

```json
{
  "schema_version": 1,
  "profile": "pypto_hard_v1",
  "instance": "kernel_name",
  "metadata": {"target": "Ascend910B"},
  "problem": {}
}
```

`dsa-bench --solution-output FILE` writes exact pool/offset placements plus a
fingerprint of the complete input. Replay requires matching schema version,
profile, instance name, and fingerprint, followed by independent validation of
all offsets. A solution for another kernel, capacity, constraint set, or
objective is rejected.

## Problem fields

- `pools`: optional capacity, reserved byte ranges, and optional bank geometry;
- `buffers`: size, alignment, half-open live intervals, and allowed pools;
- `colocations`: equal pool and offset;
- `separations`: disjoint address ranges regardless of lifetime;
- `temporal_exclusions`: pairs proven unable to coexist despite overlapping
  conservative lifetime hulls;
- `pinned_allocations`: fixed pool and offset;
- `cost_model`: optional placement costs;
- `pypto_structure`: alias and pipeline provenance; and
- `objective`: ordered lexicographic metrics.

PyPTO currently fixes every buffer to one pool. A document may bundle UB, L1,
L0A, L0B, and L0C, but these are independent DSA arenas while no constraint or
cost crosses between them.

## Reuse-cost model

`cost_model.reuse_penalties` is a sparse list of weighted buffer pairs. An edge
`(i, j, w)` contributes `w` when the buffers are temporally allowed to reuse
memory and their placed byte ranges overlap. Any positive overlap activates the
full edge cost.

The required `reason` records producer provenance only. All reasons have the
same additive optimization semantics; deciding which edges and weights are
meaningful belongs to the producer. The solver does not infer hardware behavior
from the reason name.

For PyPTO's fixed-capacity refinement, accepted placements must fit capacity and
minimize reuse cost among fitting placements. Schema v1 can represent
`capacity_overflow`, `reuse_cost`, `total_peak`, `max_peak`, and `bank_cost` as
lexicographic metrics. Overflow is useful during search or reporting; an
over-capacity result is not an accepted compiler placement.

The formulation and current evidence are in [`pypto_dsa.md`](pypto_dsa.md).

## PyPTO provenance

`alias_classes` identify semantic values already materialized as one physical
buffer. `pipeline_groups` record group, pool, requested/effective depth, slot
size, and each member's stage/residue. Provenance does not change feasibility;
hard pipeline intent is represented by typed `separations`.

`BuildPipelineIntentRelaxation` is an explicit transformation after strict
placement fails. It removes only the `pipeline_stage` reason, preserves any
other reason on the pair, adds `pipeline_serialization` penalties for fully
relaxed lifetime-compatible pairs, and selects the fit-then-reuse objective.
Ordinary solvers never relax constraints implicitly.

## Profiles

Profiles validate allowed fields; they do not imply different placement
geometry.

| Profile | Contract |
| --- | --- |
| `standard_dsa` | one pool, one interval per buffer, unit alignment, no compiler constraints/costs |
| `pypto_hard_v1` | fixed pools and conservative single intervals; validated hard constraints and provenance; peak objective |
| `pypto_research_v1` | PyPTO document with explicit experimental fields or costs |
| `pypto_structured` | readable legacy profile; new producers must not emit it |
| `pypto_core_relaxation` | explicit single-pool standard lower-bound projection |

`pypto_hard_v1` currently rejects experimental colocations, temporal
exclusions, pins, bank geometry, multi-interval buffers, flexible pool
assignment, and cost models. Mandatory semantic aliases are materialized before
export and retained as provenance.

Core relaxations record their source and every removed feature. They are lower
bounds, never compiler-valid PyPTO placements.

## Capabilities and architecture binding

`CheckSolverCompatibility` reports unsupported hard features separately from
unsupported objective terms; nothing is silently dropped.

An architecture-free PyPTO document leaves capacities unset and declares
`metadata.lowering_abi`. `dsa-bind` combines it with a versioned architecture
specification; see [`architecture_binding.md`](architecture_binding.md).
