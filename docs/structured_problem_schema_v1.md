# Structured problem schema v1

Schema v1 is the replay boundary between compiler adapters and the standalone
solver. The C++ envelope is `dsa::StructuredProblemDocument`; the
machine-readable contract is
[`schemas/dsa-problem-v1.schema.json`](../schemas/dsa-problem-v1.schema.json).
Readers reject unknown versions.

```text
compiler IR -> adapter -> schema-v1 JSON -> solver + validator
```

This document describes serialization. PyPTO semantics and the distinction
between current and experimental features are defined in
[`pypto_dsa.md`](pypto_dsa.md).

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

- `schema_version` is exactly `1`.
- `profile` selects the validated feature contract.
- `instance` is a stable benchmark identity.
- `metadata` contains provenance and required PyPTO contract identifiers; its
  values do not directly change placement feasibility.
- `relaxed_from` and `relaxed_features` appear only on named relaxations.
- `problem` contains the solver-independent model.

## Problem

```json
{
  "pools": [{
    "id": 3,
    "name": "L1",
    "capacity": 1048576,
    "reserved_ranges": [{"begin": 0, "end": 4096}]
  }],
  "buffers": [{
    "id": 7,
    "name": "lhs_tile",
    "size": 32768,
    "alignment": 512,
    "live_intervals": [{"lower": 10, "upper": 20}],
    "allowed_pools": [3]
  }],
  "constraints": {
    "colocations": [],
    "separations": [],
    "temporal_exclusions": [],
    "pinned_allocations": []
  },
  "objective": {
    "aggregation": "lexicographic",
    "terms": ["capacity_overflow", "total_peak", "max_peak"]
  }
}
```

Intervals and address ranges are half-open. Sizes, offsets, capacities, and
other byte quantities are nonnegative JSON integers. Each buffer has one fixed
size, one or more intervals, and one or more allowed pools. Multiple allowed
pools are representable but not supported by the built-in solvers.

Hard constraints are:

- `colocations`: equal pool and offset;
- `separations`: spatially disjoint regardless of lifetime, with optional
  reason provenance;
- `temporal_exclusions`: control-flow pairs proven unable to coexist;
- `pinned_allocations`: fixed pool and offset; and
- pool reservations, capacities, and any enabled whole-slot contract.

Optional bank geometry is descriptive unless `bank_cost` is requested. The
optional `cost_model.reuse_penalties` contains numeric pair costs with reason
provenance.

## PyPTO provenance

`pypto_structure` records compiler relationships in normalized form:

```json
{
  "whole_slot_reuse": true,
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

Alias members describe values already collapsed into one buffer. Pipeline
groups retain source depth and physical stage/residue membership. Except for
`whole_slot_reuse`, these fields are provenance rather than independent hard
constraints.

## Objectives

Schema v1 supports lexicographic vectors built from:

- `capacity_overflow`
- `total_peak`
- `max_peak`
- `reuse_cost`
- `bank_cost`

Terms are compared in listed order and reported individually. Weighted and
Pareto semantics require a future schema version.

## Profiles

| Profile | Meaning |
| --- | --- |
| `standard_dsa` | one fixed pool, one interval per buffer, unit alignment, no compiler extensions |
| `pypto_hard_v1` | current device-correct PyPTO contract: fixed pools, one lifetime hull, whole-slot reuse, peak objective |
| `pypto_research_v1` | hard-v1 base contract plus explicitly experimental fields or costs |
| `pypto_structured` | readable legacy profile; new producers should not emit it |
| `pypto_core_relaxation` | named per-pool standard lower bound with every removed feature recorded |

A core relaxation removes compiler-specific constraints only when doing so is
provably non-strengthening. It is rejected for flexible pools, temporal
exclusions, colocations, or overlapping intervals belonging to one physical
buffer when the standard representation would make an unsound lower-bound
claim.

## Capability matching

`CheckSolverCompatibility` reports present hard features, unsupported hard
features, and unsupported objective terms. Hard incompatibility returns
`kUnsupported`. An objective-only mismatch may be reported as a disclosed
ablation. No feature is silently removed; relaxation is a separate profile with
explicit provenance.

## Architecture-free programs

The same envelope can represent a lowered program before target binding. Pool
capacities are `null`, architecture-derived fields are absent, and
`metadata.lowering_abi` is present. `dsa-bind` combines it with a versioned
[`dsa-architecture-v1`](../schemas/dsa-architecture-v1.schema.json) document.
See [`architecture_binding.md`](architecture_binding.md).
