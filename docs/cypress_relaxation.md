# Cypress allocation-relaxation baseline

## Scope and provenance

`CypressRelaxationSolver` independently implements the resource-allocation
policy described in Yadav et al., [*Task-Based Tensor Computations on Modern
GPUs*](https://rohany.github.io/publications/pldi2025-cypress.pdf), PLDI 2025.
The contiguous packer follows Knight et al., [*Compilation for Explicitly
Managed Memory
Hierarchies*](https://theory.stanford.edu/~aiken/publications/papers/ppopp07.pdf),
PPoPP 2007.

No Cypress or Knight implementation source is copied, vendored, or linked. The
Cypress paper is CC BY 4.0; attribution is retained in `NOTICE` and
`THIRD_PARTY_NOTICES.md`.

## Published policy

Cypress addresses a capacity-versus-parallelism trade-off in asynchronous GPU
shared memory:

1. build the mandatory tensor-interference graph;
2. add auxiliary edges to make each memory space's graph complete, initially
   prohibiting all aliasing;
3. try to pack the graph within the user-provided capacity;
4. remove auxiliary edges and retry until packing succeeds;
5. insert WAR dependencies for tensors that reuse one physical allocation.

Only steps 1--4 are a DSA solver. Dependency insertion belongs to Cypress's
compiler and, for PyPTO, to downstream PTOAS correctness handling.

For each attempted graph, the Knight packer:

1. initializes every allocation at address zero;
2. selects the unfinished allocation at the lowest address, breaking ties by
   the largest number of interference neighbors, then
   larger size, then stable buffer ID;
3. finalizes it and shifts every still-overlapping interference neighbor above
   its range; and
4. repeats until all allocations are fixed.

## Frozen underspecified choice

The Cypress paper does not specify which auxiliary edge is removed next or
publish allocator conformance cases. This baseline freezes a reproducible
choice:

```text
deletion order = increasing pool ID, then increasing endpoint buffer IDs
```

It reports the complete auxiliary-edge count, removed prefix length, packing
attempts, and the number of removed pairs that actually overlap in the final
geometry. Removing an edge permits reuse but does not guarantee it.

Consequently, this implementation makes no global minimum-aliasing claim. The
first feasible graph is minimal only along its fixed deletion path, and the
inner packer is heuristic.

## Capability boundary

This is a capacity baseline, not a peak-minimization baseline. Every independent
pool must have a fixed capacity. The implementation accepts fixed pools,
pre-materialized colocation classes, hard separations, per-buffer alignment,
and reserved address ranges. Multiple pools are solved independently.

It rejects flexible pool assignment, multi-interval liveness, temporal
exclusions, pinned allocations, and bank geometry. Reuse weights do not affect
the unweighted Cypress deletion order and this limitation is reported. Per-buffer
alignment, reserved ranges, and multiple independent pools are disclosed
adaptations to the common DSA interface; the papers describe one explicitly
managed memory with target alignment.

Use `dsa-bench --solver cypress-relaxation` with a capacity-bearing structured
problem or `--capacity` for single-pool CSV input. Reports use
`cypress_relaxation`.

## Interpretation

Cypress is directly relevant to PyPTO's research question because both prefer
less physical aliasing subject to a hard memory limit. It is intentionally a
coarser baseline:

- every lifetime-compatible pair begins as equally undesirable;
- deleted edges are unweighted;
- edge deletion, rather than actual final overlap, drives the outer loop; and
- synchronization topology is not part of the objective.

Comparing this baseline with weighted, geometry-activated reuse penalties
measures whether compiler-grounded hazard information improves over generic
minimum-aliasing pressure.
