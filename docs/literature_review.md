# Static DSA literature

## Problem boundary

Three problems should remain distinct:

1. fixed-schedule DSA: sizes and lifetimes are given; choose offsets minimizing
   peak or fitting capacity;
2. compiler-structured allocation: add aliases, alignment, reservations, and
   other hard or soft compiler facts;
3. schedule/allocation co-optimization: choose operation order and placement
   together.

The standard benchmark evaluates the first. PyPTO currently addresses the
second; see [`pypto_dsa.md`](pypto_dsa.md).

## Implemented baselines

| Method | Standard DSA | Structured PyPTO | Role |
| --- | --- | --- | --- |
| first fit | direct | structural baseline | deterministic fallback |
| XLA spatial best fit | direct | explicit relaxation only | frozen heap baseline |
| TVM hill climb | direct | compatible ablation | frozen ordering-search baseline |
| generic local search | direct | compatible ablation | generic ordering-search baseline |
| MiniMalloc exact | direct | explicit relaxation only | optimum or bounded oracle |
| PyPTO structured search | no | direct | experimental structured moves |

Every result uses this repository's model and independent validator. A named
reimplementation identifies the published policy being studied, not bitwise
compatibility or project endorsement. Provenance and licensing are recorded in
`NOTICE` and `THIRD_PARTY_NOTICES.md`.

## OpenXLA

OpenXLA's
[`GlobalDecreasingSizeBestFitHeap`](https://github.com/openxla/xla/blob/604c077cd1b4c749595ca3d881ee550a25dc82ff/xla/service/heap_simulator/heap_simulator.h)
orders buffers by size and lifetime duration, then chooses the smallest free
chunk that fits. XLA also separates HLO scheduling from heap simulation while
using memory estimates to compare legal schedules.

`xla_heap` freezes only the comparable spatial policy. Sliced allocations,
preferred offsets, memory-space assignment, and constrained multi-heap variants
are outside its contract. See [`xla_heap.md`](xla_heap.md).

## Apache TVM

TVM's
[`Unified Static Memory Planning RFC`](https://github.com/apache/tvm-rfcs/blob/8e5c1250a6632033c8ffa2d901b3a4b0ce59f982/rfcs/0009_Unified_Static_Memory_Planning.md)
defines a replaceable planner over buffer sizes, conflicts, alignments, and
candidate pools. TVM v0.14's
[`hill_climb.cc`](https://github.com/apache/tvm/blob/v0.14.0/src/tir/usmp/algo/hill_climb.cc)
searches greedy placement orders with graph-guided swaps and occasional
decaying acceptance of worse moves.

`tvm_hill_climb` freezes that neighborhood. `local_search` adds generic
insertion, reversal, and restart behavior. See
[`tvm_hill_climb.md`](tvm_hill_climb.md).

## Exact and production-oriented work

[Google MiniMalloc](https://github.com/google/minimalloc) exactly solves its
single-pool, fixed-size, single-interval contract and supplies the adversarial
A--K benchmark family. A timed-out run provides only a feasible upper bound,
not an optimum certificate. Structured PyPTO projections are lower bounds, not
compiler-valid placements.

[*TelaMalloc*](https://doi.org/10.1145/3567955.3567961) evaluates production
on-chip allocation where compilation latency and solution quality both matter.
It motivates explicit search budgets and separation of bounded compiler
heuristics from offline oracles. No TelaMalloc implementation is included.

TFLite Micro's
[`GreedyMemoryPlanner`](https://github.com/tensorflow/tflite-micro/blob/096563546742ba81adb6f012ab718d196a48e02d/tensorflow/lite/micro/memory_planner/greedy_memory_planner.cc)
is a deterministic gap-searching arena allocator and illustrates the value of a
predictable fallback.

## Checked-in comparison

The [`standard-v1` report](../benchmarks/results/standard-v1/report.md) compares
MiniMalloc exact, first fit, XLA heap, TVM hill climb, and local search on the
public MiniMalloc A--K set and deduplicated per-pool standard projections of the
PyPTO corpora. Compiler-specific reuse-cost conclusions require separate
structured and device experiments.
