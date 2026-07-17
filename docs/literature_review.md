# Static DSA literature

## Scope

Three problems should not be conflated:

1. **Fixed-schedule DSA:** sizes and lifetimes are given; choose offsets that
   minimize peak or fit a capacity.
2. **Compiler-structured allocation:** add aliases, pools, alignment, hardware
   separations, and other compiler constraints.
3. **Schedule/allocation co-optimization:** operation order and placement are
   chosen together.

The standard benchmark evaluates the first problem. The PyPTO variant addresses
the second and may later become the inner solver for the third. Its definition
is in [`pypto_dsa.md`](pypto_dsa.md).

## OpenXLA

OpenXLA's
[`GlobalDecreasingSizeBestFitHeap`](https://github.com/openxla/xla/blob/604c077cd1b4c749595ca3d881ee550a25dc82ff/xla/service/heap_simulator/heap_simulator.h)
sorts buffers by decreasing size and lifetime duration, then selects the
smallest free chunk that fits. The policy is implemented in
[`heap_simulator.cc`](https://github.com/openxla/xla/blob/604c077cd1b4c749595ca3d881ee550a25dc82ff/xla/service/heap_simulator/heap_simulator.cc).

XLA also illustrates a useful architecture: buffer assignment consumes an HLO
schedule, while
[`HloMemoryScheduler`](https://github.com/openxla/xla/blob/604c077cd1b4c749595ca3d881ee550a25dc82ff/xla/hlo/transforms/simplifiers/hlo_memory_scheduler.cc)
uses heap simulation to compare legal schedules. Scheduling and placement stay
separate but exchange memory estimates. This is a practical model for a future
PyPTO outer scheduling loop.

`xla_heap` freezes the comparable spatial policy. Sliced allocations, preferred
offsets, constrained multi-heap variants, and memory-space assignment remain
outside its benchmark contract; see [`xla_heap.md`](xla_heap.md).

## Apache TVM

TVM's
[`Unified Static Memory Planning RFC`](https://github.com/apache/tvm-rfcs/blob/8e5c1250a6632033c8ffa2d901b3a4b0ce59f982/rfcs/0009_Unified_Static_Memory_Planning.md)
made the planner replaceable and represented conflicts and candidate pools
explicitly. TVM v0.14's
[`hill_climb.cc`](https://github.com/apache/tvm/blob/v0.14.0/src/tir/usmp/algo/hill_climb.cc)
searches buffer orderings with conflict-guided swaps and a decaying probability
of accepting worse candidates.

`tvm_hill_climb` freezes that policy as an ablation baseline. Generic
`local_search` adds insertion, reversal, and restarts; PyPTO structured search
adds compiler-derived neighborhood units. See
[`tvm_hill_climb.md`](tvm_hill_climb.md).

## MiniMalloc

[MiniMalloc](https://github.com/google/minimalloc) is an exact solver for the
single-pool, fixed-size, single-interval problem. It searches canonical
solutions, applies spatial inference, and removes dominated states. The paper
is Michael D. Moffitt,
[*MiniMalloc: A Lightweight Memory Allocator for Hardware-Accelerated Machine
Learning*](https://doi.org/10.1145/3623278.3624752), ASPLOS 2023.

MiniMalloc is ground truth where its contract applies. Its A--K instances are
adversarial one-MiB tests. Results on relaxed PyPTO pools are lower bounds, not
compiler-valid placements.

## TelaMalloc

Maas, Beaugnon, Chauhan, and Ilbeyi's
[*TelaMalloc: Efficient On-Chip Memory Allocation for Production Machine
Learning Accelerators*](https://doi.org/10.1145/3567955.3567961), ASPLOS 2023,
evaluates production on-chip allocation where both compilation latency and
solution quality matter. Its combination of fast heuristics, constraint
solving, and targeted backtracking motivates reporting explicit budgets and
keeping an offline oracle separate from the bounded compiler heuristic.

No drop-in TelaMalloc implementation is included here. Comparisons must use
published results or a separately documented reimplementation.

## TensorFlow Lite Micro

TFLite Micro's
[`GreedyMemoryPlanner`](https://github.com/tensorflow/tflite-micro/blob/096563546742ba81adb6f012ab718d196a48e02d/tensorflow/lite/micro/memory_planner/greedy_memory_planner.cc)
orders long buffers and greedily searches gaps in one arena. It demonstrates
why a deterministic allocator remains valuable as a predictable fallback even
when stronger search is available.

## Comparison

| Method | Standard DSA | PyPTO direct | Role |
| --- | --- | --- | --- |
| first fit | yes | yes | deterministic fallback |
| Cypress relaxation | capacity form | capacity form | unweighted anti-alias relaxation baseline |
| XLA spatial best fit | yes | no | frozen heap baseline |
| TVM hill climb | yes | compatible ablation | frozen ordering-search baseline |
| generic local search | yes | compatible ablation | generic neighborhood baseline |
| MiniMalloc exact | yes | relaxation only | optimum or lower-bound oracle |
| PyPTO structured search | no | yes | experimental structured neighborhoods |

All named reimplementations use this repository's model and validator. Their
names identify the published policy being studied, not bit-for-bit compatibility
or project endorsement. Licensing and provenance are recorded in `NOTICE` and
`THIRD_PARTY_NOTICES.md`.

The checked-in
[`standard-v1` report](../benchmarks/results/standard-v1/report.md) compares
MiniMalloc exact, first fit, XLA heap, TVM hill climb, and local search on the
public MiniMalloc A--K set and deduplicated per-pool projections of PyPTO and
PyPTO-Lib instances. Structured conclusions are intentionally deferred.

## Cypress

Cypress starts from a complete tensor-interference graph, removes auxiliary
anti-alias edges until a Knight-style contiguous allocation fits a fixed shared
memory budget, and then inserts dependencies required by selected physical
reuse. It is the closest published policy baseline for PyPTO's
capacity-versus-synchronization question, but it is unweighted and its edge
deletion order is not specified in the paper. See
[`cypress_relaxation.md`](cypress_relaxation.md) for the frozen implementation
boundary.
