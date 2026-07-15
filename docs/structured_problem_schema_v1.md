# Structured problem schema v1

The structured JSON format is the replay boundary between compiler adapters and the standalone solver
framework. A compiler emits portable data; this repository never depends on compiler IR types.

```text
PyPTO IR -> PyPTO adapter -> schema-v1 JSON / DsaProblem -> solver + validator
```

The C++ envelope is `dsa::StructuredProblemDocument`; the machine-readable contract is
[`schemas/dsa-problem-v1.schema.json`](../schemas/dsa-problem-v1.schema.json). JSON readers reject unknown
schema versions so a document's semantics cannot change silently.

## Top-level envelope

```json
{
  "schema_version": 1,
  "profile": "pypto_hard_v1",
  "instance": "kernel_name",
  "metadata": {
    "target": "ascend910b"
  },
  "problem": {}
}
```

Fields:

- `schema_version`: exactly `1` for this document.
- `profile`: `standard_dsa`, `pypto_hard_v1`, `pypto_research_v1`, legacy
  `pypto_structured`, or `pypto_core_relaxation`.
- `instance`: stable benchmark-instance name.
- `metadata`: string-to-string envelope data. Most keys are provenance and never affect feasibility;
  versioned PyPTO profiles additionally require their event-ordering, solver-input, and address-reuse
  contract keys so those semantics cannot be omitted silently. The values still do not change a placement.
- `relaxed_from`: required for `pypto_core_relaxation`, forbidden for the other profiles.
- `relaxed_features`: structure deliberately removed by a core relaxation.
- `problem`: the solver-independent DSA problem.

## Problem core

```json
{
  "pools": [
    {
      "id": 3,
      "name": "L1",
      "capacity": 1048576,
      "reserved_ranges": [{"begin": 0, "end": 4096}],
      "bank_geometry": {"bank_size": 256, "num_banks": 16}
    }
  ],
  "buffers": [
    {
      "id": 7,
      "name": "lhs_tile",
      "size": 32768,
      "alignment": 512,
      "live_intervals": [{"lower": 10, "upper": 20}],
      "allowed_pools": [3]
    }
  ],
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

All intervals and address ranges are half-open. Every buffer has one fixed `size` and one or more
`live_intervals`; a solver jointly chooses byte offsets and may subdivide an expired address region among
smaller later buffers unless an explicit compiler structure requests whole-slot reuse. Schema v1 does not
represent lifetime-dependent resizing or sliced allocations.
Unsigned byte quantities must be JSON integers; floating point and negative representations are rejected.

`allowed_pools` with one entry is a fixed placement space. Multiple entries represent flexible pool
assignment, which is part of the portable model but currently unsupported by the built-in solvers.

### Hard constraints

- `colocations`: `{first, second}` pairs that must receive exactly the same pool and offset.
- `separations`: pairs whose byte ranges must not overlap even if their lifetimes are disjoint. The optional
  `reasons` array preserves `generic`, `pipeline_stage`, `target_hazard`, or `semantic_no_alias` provenance;
  every solver enforces the same pair regardless of its reason.
- `temporal_exclusions`: pairs proven unable to coexist on one control-flow path despite overlapping
  conservative intervals.
- `pinned_allocations`: `{buffer, pool, offset, exclusive_for_all_time}` records.

Reserved ranges are pool-level hard constraints. Bank geometry is descriptive until a requested objective
and solver both support bank cost.

### Cost overlay

The optional cost model currently carries pairwise reuse penalties:

```json
{
  "cost_model": {
    "reuse_penalties": [
      {"first": 1, "second": 2, "cost": 40, "reason": "cross_pipe"}
    ]
  }
}
```

Reasons are `generic`, `cross_pipe`, `cross_core`, and `event_budget`. They preserve provenance for
analysis; the numeric `cost` is what the current objective evaluator consumes.

### Normalized PyPTO structure

A PyPTO document may carry compiler relationships in addition to their portable translation:

```json
{
  "pypto_structure": {
    "whole_slot_reuse": true,
    "alias_classes": [
      {"buffer": 7, "members": ["tile", "slice", "reshape"]}
    ],
    "pipeline_groups": [
      {
        "group": 4,
        "pool": 3,
        "slot_size": 32768,
        "depth": 3,
        "effective_depth": 2,
        "members": [
          {"buffer": 7, "stage": 0, "residue": 0}
        ]
      }
    ]
  }
}
```

Alias classes name the IR values already collapsed into one fixed-size buffer. Pipeline groups retain the
source depth and the capacity-gated physical residue count. `whole_slot_reuse` is an explicit feasibility
constraint: buffers may reuse an equal base address (with the slot sized to the largest member) or occupy
disjoint address ranges, but may not partially overlap at different base addresses. It defaults to `false`
when absent for compatibility with earlier schema-v1 documents. The remaining fields are normalized
provenance for PyPTO-aware search moves and benchmark analysis.

## Objectives

Schema v1 deliberately supports only lexicographic objective vectors. It does not invent weights between
bytes, synchronization, events, and bank effects.

Available terms are:

- `capacity_overflow`
- `total_peak`
- `max_peak`
- `reuse_cost`
- `bank_cost`

Terms are compared in their listed order. Every raw component is also reported by `dsa-bench`. Weighted
and Pareto objectives require a future schema version with explicit semantics.

## Benchmark profiles

### `standard_dsa`

Directly comparable with MiniMalloc:

- exactly one pool;
- one interval and one fixed pool per buffer;
- alignment one;
- no reserved ranges, banks, compiler constraints, pins, cost overlay, or PyPTO structure;
- only capacity and peak objective terms.

MiniMalloc CSV input is wrapped in this profile internally.

### `pypto_hard_v1`

Carries only the current production correctness contract. It requires one fixed pool and one conservative
interval per buffer, whole-slot reuse, the peak objective, and the explicit PyPTO metadata contracts. It
permits alignment, multiple fixed pools, reservations, separations, and normalized alias/pipeline
provenance. It rejects cost models, banks, colocations, temporal exclusions, pins, flexible pools, and
multi-interval lifetimes. See [`pypto_hard_v1.md`](pypto_hard_v1.md) for the formal constraints and field
audit.

### `pypto_research_v1`

Requires the same PyPTO event-ordering, input-stage, and whole-slot contracts, but permits experimental
hard features and cost overlays. The profile does not make those fields production requirements. The
current adjacent pipeline reuse proxy uses this profile because its unit cost has not been device
calibrated.

### `pypto_structured` (legacy)

Kept readable for existing schema-v1 artifacts. It predates the hard/research distinction and should not
be emitted by new producers.

### `pypto_core_relaxation`

A standard-DSA lower-bound problem derived from one fixed pool of any PyPTO document. The library:

- partitions fixed pools into independent documents;
- removes alignment, reservations, banks, pins, colocations, separations, costs, and PyPTO provenance;
- splits a multi-interval buffer into independent standard rows;
- resets the objective to peak minimization;
- records every removed feature and source pool in the envelope.

For the accepted subset, removing constraints and non-overlapping cross-interval identity enlarges the
feasible set, so the resulting peak is a lower bound. It must never be presented as a valid PyPTO
placement.

Schema v1 rejects four projections rather than making an unsound claim:

- flexible pool assignment, because selecting a pool before solving may strengthen the problem;
- temporal exclusions, because arbitrary path-exclusion graphs cannot be represented faithfully by one
  interval per MiniMalloc row.
- colocations, because turning simultaneously-live colocated buffers into independent rows can increase
  the relaxed peak;
- overlapping intervals belonging to one buffer, because splitting them creates multiple simultaneously
  live allocations where the source requires only one allocation.

## Capability matching

`CheckSolverCompatibility` reports:

- structure present in the problem;
- hard features the solver cannot honor;
- requested objective terms the solver does not optimize.

Hard incompatibility produces `kUnsupported`. An objective-only mismatch may still be useful as a
disclosed structural baseline: first-fit runs and reports `objective_compatible=false`. Search solvers
whose candidate ranking depends on the requested objective reject unsupported objective terms.

No feature or objective is silently removed. Relaxation is a separate, named profile with explicit
provenance.
