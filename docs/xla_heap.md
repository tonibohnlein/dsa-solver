# OpenXLA heap-simulator baseline

## Scope and provenance

`XlaHeapSolver` is an independent behavioral reimplementation of OpenXLA's
spatial `GlobalDecreasingSizeBestFitHeap`. The pinned primary reference is
OpenXLA commit
[`604c077cd1b4c749595ca3d881ee550a25dc82ff`](https://github.com/openxla/xla/tree/604c077cd1b4c749595ca3d881ee550a25dc82ff):

- [`heap_simulator.h`](https://github.com/openxla/xla/blob/604c077cd1b4c749595ca3d881ee550a25dc82ff/xla/service/heap_simulator/heap_simulator.h)
  defines `BufferInterval`, packing strategies, and the free-chunk interface;
- [`heap_simulator.cc`](https://github.com/openxla/xla/blob/604c077cd1b4c749595ca3d881ee550a25dc82ff/xla/service/heap_simulator/heap_simulator.cc)
  defines the spatial comparator, free-chunk construction, and smallest-fitting
  chunk choice;
- [`heap_simulator_test.cc`](https://github.com/openxla/xla/blob/604c077cd1b4c749595ca3d881ee550a25dc82ff/xla/service/heap_simulator/heap_simulator_test.cc)
  provides the decreasing-size, alignment, best-fit, and colocation examples.

OpenXLA is Apache-2.0 licensed. This repository does not vendor or link XLA.
The implementation uses dsa-solver's model, placement validation, and data
structures; `NOTICE` and `THIRD_PARTY_NOTICES.md` retain attribution.
The OpenXLA name identifies the provenance of the published policy only; this
project is not affiliated with or endorsed by the OpenXLA project.

## Frozen policy

The baseline deliberately freezes the recognizable spatial policy:

1. form one placement node per colocation class;
2. order nodes by decreasing size, then decreasing lifetime-hull duration,
   then stable buffer ID;
3. collect address ranges occupied by already placed temporal conflicts;
4. construct aligned free chunks between those ranges;
5. choose the smallest free chunk that fits, breaking equal-size ties toward
   the lowest offset;
6. use the unbounded final chunk only when no finite free chunk fits.

The conformance tests reproduce OpenXLA's published expected offsets for
`DecreasingSize`, `DecreasingSizeWithAlignment`, and `BestFit`.

## Capability boundary

The XLA baseline is intentionally not generalized until it becomes
indistinguishable from our native heuristic. It accepts:

- a single fixed pool;
- one half-open interval per buffer;
- positive alignment;
- optional colocations;
- peak/capacity objectives.

It rejects multi-interval liveness, separations, temporal exclusions, pins,
reserved ranges, reuse costs, and flexible pools.
Structured compiler inputs are compared through explicit per-pool core
relaxations. An unsupported feature produces `kUnsupported`; it is never
silently dropped.

Differences forced by the common interface are recorded rather than hidden:

- OpenXLA receives allocation/free events; dsa-solver receives equivalent
  half-open lifetimes.
- OpenXLA's constructor takes one heap alignment. The portable DSA model keeps
  alignment per buffer, and the same best-fit policy applies each requested
  alignment. Colocated buffers use the overflow-checked least common multiple
  of their positive alignments.
- XLA sliced allocations, preferred offsets, constrained multi-heap variants,
  and memory-space-assignment extensions are outside this baseline.

The command-line method name is `xla-heap`; reports use `xla_heap`.
