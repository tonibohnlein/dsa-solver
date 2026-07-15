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

There is no whole-slot contract. Lifetime-disjoint buffers may partially overlap
at different bases, as required by ordinary DSA and issue #1908.

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
Pipeline groups record source depth and stage/residue membership. Hard pipeline
requirements are represented separately as typed `separations`; provenance
alone does not change feasibility.

## Optional cost model

`cost_model.reuse_penalties` contains sparse buffer pairs. A cost is charged
when the pair has disjoint lifetimes but overlapping placed address ranges.
Reasons distinguish evidence:

- `pipeline_serialization`: PR #1949-style false WAR between pipeline stages;
- `cross_pipe`, `cross_core`, `event_budget`: reserved for models calibrated
  against PTOAS output and device measurements; and
- `generic`: test or externally supplied cost with no stronger claim.

Schema v1 objectives are lexicographic vectors using `capacity_overflow`,
`total_peak`, `max_peak`, `reuse_cost`, and `bank_cost`. Raw values are retained;
weighted and Pareto semantics require a future version.

## Profiles

| Profile | Contract |
| --- | --- |
| `standard_dsa` | literature-compatible single-pool problem |
| `pypto_hard_v1` | compiler capture using standard DSA geometry plus validated hard constraints and provenance |
| `pypto_research_v1` | the same capture with explicit experimental constraints or costs |
| `pypto_structured` | readable legacy profile; new producers should not emit it |
| `pypto_core_relaxation` | named standard lower-bound projection with removed features recorded |

The mathematical distinction is documented in [`pypto_dsa.md`](pypto_dsa.md):
only experimental features that change feasibility or the objective define a
research refinement.

## Capability matching and architecture binding

`CheckSolverCompatibility` reports unsupported hard features and objective
terms; nothing is silently dropped. A core relaxation is a separate document.

An architecture-free capture leaves capacities unset and records
`metadata.lowering_abi`. `dsa-bind` combines it with a versioned architecture
document; see [`architecture_binding.md`](architecture_binding.md).
