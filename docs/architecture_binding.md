# Program and architecture binding

`dsa-bind` and the C++ `BindArchitecture` API combine a lowered program variant
with versioned target resources. The result is an ordinary schema-v1 solver
input.

## Identity

For workload `W`, architecture `A`, and compiler lowering through DSA
collection:

```text
G[W,A] = Lower(W, A)
I[W,A] = Bind(G[W,A], A)
```

The benchmark records three identities:

1. `workload_id`: source entry point, parameters, and source/compiler commits;
2. `program_fingerprint`: lowered buffers, logical memory spaces, lifetimes,
   constraints, and objective, excluding target resources;
3. `architecture_fingerprint`: the versioned resource specification.

The runnable instance key is `(program_fingerprint,
architecture_fingerprint)`. One `workload_id` may produce different lowered
programs for different targets.

## Architecture-free program

An unbound PyPTO document contains buffers, fixed logical memory spaces,
lifetimes, constraints, provenance, and objective, but no target resources. It
must:

- use a PyPTO profile;
- give every pool a unique logical name and `capacity: null`;
- omit architecture bank geometry, target identity, and binding fingerprints;
- declare a non-empty `metadata.lowering_abi`.

Numeric pool IDs are serialization-local. Program fingerprints use logical
space names so harmless ID renumbering does not change identity.

## Architecture specification

Files in `benchmarks/architectures/` map logical memory spaces to:

- usable and physical capacities;
- minimum alignment;
- reserved ranges;
- optional bank geometry;
- supported lowering ABIs; and
- source provenance.

Usable capacity is authoritative for allocation. It may be lower than physical
capacity because of compiler or runtime reservations. The checked-in Ascend
910B and 950 specifications pin their values and 32-byte allocation policy to a
specific PyPTO revision; the JSON files, not product literature, are the source
of truth.

## Binding

`Bind(G,A)`:

1. checks that `A` supplies every required logical space and supports the
   lowering ABI;
2. creates one solver pool per used logical space;
3. applies usable capacities, reserved ranges, and architecture alignment;
4. preserves buffers, lifetimes, fixed-pool membership, constraints, and
   objective;
5. attaches program/architecture fingerprints and target provenance; and
6. validates the resulting structured document.

The binder appends `@<architecture_id>` to the instance name. Fingerprints are
deterministic FNV-1a-64 identifiers for benchmark identity and deduplication,
not cryptographic authentication.

```bash
./build/dsa-bind \
  --program tests/data/pypto_unbound_program_v1.json \
  --architecture benchmarks/architectures/ascend950-v1.json \
  --output /tmp/program-ascend950.json
```

## When rebinding is valid

Changing only the architecture file is a real target comparison only when
lowering produces the same architecture-free program fingerprint. PyPTO may
make target-dependent tiling, memory-space, layout, writeback, and pipeline
decisions before DSA collection. If those decisions differ, the two lowered
graphs are distinct program variants under the same `workload_id`.

Binding a 910B graph to 950 resources can still be useful as a counterfactual
capacity experiment, but it is not a target-correct 950 compiler capture.

## Independent arenas

With fixed pool membership and objectives built only from independent arena
peaks, each physical memory space is an independent DSA problem. Minimizing
every arena peak also minimizes their sum and maximum. Cross-pool coupling would
require a new decision such as flexible pool assignment, shared capacity, a
global event budget, or pipeline depth that changes several arenas together.
