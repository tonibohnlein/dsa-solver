# Program and architecture binding

## Status

The architecture binder is implemented by `dsa-bind` and the C++
`BindArchitecture` API. Schema v1 remains the final, fully bound solver input;
the binder also uses that envelope for an unbound program, with capacities set
to `null` and target metadata omitted. Versioned architecture resources live in
`architectures/` and are validated against
`schemas/dsa-architecture-v1.schema.json`.

## Three identities, not one filename

Let `W` be a source workload and compile configuration, `A` an architecture,
and `Lower` the compiler pipeline up to DSA collection. The actual allocation
graph is

```text
G[W,A] = Lower(W, A)
I[W,A] = Bind(G[W,A], A)
```

The benchmark should therefore record three stable identities:

1. `workload_id`: source entry point, parameters, and source/compiler commits;
2. `program_fingerprint`: the lowered buffers, sizes, logical memory spaces,
   lifetimes, and constraints, excluding architecture capacities;
3. `architecture_fingerprint`: the versioned resource specification.

The runnable benchmark key is the pair
`(program_fingerprint, architecture_fingerprint)`. `workload_id` groups
different target-dependent lowerings of the same source program.

## Lowered program variant

An architecture-unbound program variant contains:

- buffers, sizes, and stable PyPTO logical memory spaces (`Vec`, `Mat`, `Left`,
  `Right`, `Acc`), corresponding to UB, L1, L0A, L0B, and L0C;
- half-open live intervals in the fixed compiler schedule;
- semantic alignment requirements that do not come from the target;
- separations, alias identities, and pipeline structure;
- the objective policy, but no target capacity;
- a lowering ABI or compatibility set describing architectures on which this
  exact lowered graph is valid.

Numeric pool IDs are serialization-local. Fingerprints use logical space names
so harmless ID renumbering does not create a new program.

Concretely, an unbound program must:

- use a PyPTO profile and pass the normal structured-problem validation;
- have `capacity: null` for every pool and no architecture bank geometry;
- contain a non-empty `metadata.lowering_abi`;
- omit `target`, architecture identity, and binding fingerprints;
- use a unique, non-empty logical name for each pool.

## Architecture specification

An architecture specification is a small versioned document keyed by logical
memory space. Each entry can provide:

- usable capacity used by compiler allocation;
- physical capacity for information only;
- minimum address alignment;
- compiler/runtime-reserved address ranges;
- optional bank geometry and other offset-dependent properties;
- provenance: repository, commit, backend name, and specification source.

Usable capacity is authoritative for feasibility. It must not be inferred from
a marketing capacity when the compiler or ISA reserves part of the space. At
PyPTO commit `8df2ed4b`, the backend contract is:

| Logical space | Ascend 910B usable | Ascend 950 usable |
| --- | ---: | ---: |
| UB | 188,416 B (184 KiB) | 245,760 B (240 KiB) |
| L1 | 524,288 B | 524,288 B |
| L0A | 65,536 B | 65,536 B |
| L0B | 65,536 B | 65,536 B |
| L0C | 131,072 B | 262,144 B |

The UB limits are deliberately below physical capacity because the backend
reserves the top region. Architecture specifications must pin this provenance
instead of copying unversioned constants from product literature. Both current
specifications use the compiler's 32-byte default allocation policy from
`include/pypto/ir/memory_allocator_policy.h`; this is distinct from descriptive
SoC memory alignment fields that the planner does not currently apply.

## Binding

`Bind(G,A)` performs a deterministic, independently validated transformation:

1. verify that `A` supplies every logical space required by `G` and that the
   lowering ABI is compatible;
2. create one solver pool per used logical space;
3. set the pool's usable capacity and reserved ranges from `A`;
4. combine program and architecture alignment requirements;
5. retain sizes, lifetimes, fixed-pool membership, constraints, and objective;
6. attach both fingerprints and architecture provenance;
7. validate the resulting bound `StructuredProblemDocument`.

The output is the existing solver-facing shape. The binder appends
`@<architecture_id>` to the instance name and records
`program_fingerprint_fnv1a64`, `architecture_fingerprint_fnv1a64`,
`architecture_id`, and `target`. Solvers do not need awareness of source
programs or architecture catalogs.

The program fingerprint excludes source-only metadata, target resources, and
serialization-local pool IDs. The architecture fingerprint canonicalizes ABI
and memory-space ordering. Both are deterministic 64-bit FNV-1a hexadecimal
identifiers for benchmark identity and deduplication, not cryptographic content
authentication.

## Command-line use

```bash
./build/dsa-bind \
  --program tests/data/pypto_unbound_program_v1.json \
  --architecture architectures/ascend950-v1.json \
  --output /tmp/program-ascend950.json
```

The checked-in portable test graph declares
`pypto-capacity-portable-v1`, which both specifications support. A real a2a3
capture should declare `pypto-a2a3-v1`; binding that graph to Ascend 950 is
rejected unless independent compiler evidence justifies a compatible ABI.

## When capacity-only rebinding is valid

Two architectures may share one lowered program variant only if lowering
produces the same architecture-free program fingerprint. This must be checked,
not assumed. PyPTO currently makes target-dependent decisions before DSA:

- matmul tile selection consumes L0 capacities and performance parameters;
- memory-space and cross-core lowering can differ by backend;
- write-back and layout adaptation can introduce different buffers;
- pipeline-depth decisions may depend on available local memory.

If these decisions differ, `G[W,910B]` and `G[W,950]` are separate program
variants grouped by one `workload_id`. Binding a 910B graph to 950 capacity can
still be useful, but it must be labeled a counterfactual capacity-sensitivity
experiment rather than a real 950 compiler instance.

## Multi-pool separability

For fixed pool membership, let `B_p` be the buffers assigned to pool `p` and
`F_p` its feasible placements. The complete feasible set is

```text
F = product over p of F_p.
```

The current peak objective uses the vector of independent pool peaks through
`sum_p H_p` and `max_p H_p`. Minimizing every `H_p` independently minimizes
both aggregates. Thus a four-pool matmul document is an exact composition of
four per-pool problems, although an individual pool may still carry PyPTO
alignment, whole-slot, or separation constraints beyond standard DSA.

Separability ends when the model permits flexible pool assignment, shared
capacity, a global event budget, or a decision such as pipeline depth that
simultaneously changes requirements in several pools.

## Remaining corpus and reporting work

A staged integration should:

1. make the PyPTO exporter emit architecture-free program variants alongside
   its existing bound captures;
2. represent suite inputs as `(program, architecture)` bindings;
3. report `workload_id`, `program_fingerprint`, and `architecture_id` as
   separate columns;
4. compile each workload for both targets and compare architecture-free
   fingerprints before deciding whether one program variant can be shared;
5. keep counterfactual capacity sweeps in a separate benchmark profile.

This avoids duplicating identical allocation graphs while preventing a
capacity edit from being mistaken for a target-correct compiler capture.
