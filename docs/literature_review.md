# Static DSA literature and PyPTO research map

## Scope

This review separates three questions that are often conflated:

1. **Fixed-schedule DSA:** lifetimes and sizes are given; choose offsets that
   minimize height or fit a capacity.
2. **Compiler-structured allocation:** the fixed-schedule problem also carries
   aliases, pools, alignment, pins, hardware separations, and reuse costs.
3. **Schedule/allocation co-optimization:** operation order changes lifetimes,
   so the compiler chooses both a schedule and a placement.

`standard_dsa` benchmarks question 1. `pypto_structured` benchmarks question 2.
Question 3 is a later research layer and must not be evaluated by pretending
that one fixed exported schedule represents the joint optimum.

## Primary systems

### OpenXLA heap simulator

OpenXLA's
[`GlobalDecreasingSizeBestFitHeap`](https://github.com/openxla/xla/blob/604c077cd1b4c749595ca3d881ee550a25dc82ff/xla/service/heap_simulator/heap_simulator.h)
records size, allocation/free times, and colocations. Its spatial policy sorts
by decreasing size and decreasing duration, then places into the smallest free
chunk that fits. The implementation and expected placements are in
[`heap_simulator.cc`](https://github.com/openxla/xla/blob/604c077cd1b4c749595ca3d881ee550a25dc82ff/xla/service/heap_simulator/heap_simulator.cc)
and
[`heap_simulator_test.cc`](https://github.com/openxla/xla/blob/604c077cd1b4c749595ca3d881ee550a25dc82ff/xla/service/heap_simulator/heap_simulator_test.cc).

The important architectural lesson is not only the heuristic. XLA's buffer
assignment consumes a chosen HLO schedule, while its
[`HloMemoryScheduler`](https://github.com/openxla/xla/blob/604c077cd1b4c749595ca3d881ee550a25dc82ff/xla/hlo/transforms/simplifiers/hlo_memory_scheduler.cc)
uses buffer sizes and heap simulation when comparing legal schedules.
Scheduling and placement therefore remain separate algorithms connected by a
memory estimate rather than one monolithic solver. This is a practical template
for PyPTO: first make fixed-schedule DSA sound and measurable, then let
scheduling query the allocator in a bounded outer loop.

The `xla_heap` implementation in this repository freezes the comparable core
policy. XLA's sliced allocations, preferred offsets, constrained multi-heap
variants, and memory-space-assignment prefetch decisions are deliberately out
of scope; see [`xla_heap.md`](xla_heap.md).

### Apache TVM Unified Static Memory Planning

TVM's
[`Unified Static Memory Planning RFC`](https://github.com/apache/tvm-rfcs/blob/8e5c1250a6632033c8ffa2d901b3a4b0ce59f982/rfcs/0009_Unified_Static_Memory_Planning.md)
made the planner replaceable and represented conflicts and pool candidates
explicitly. TVM v0.14's
[`hill_climb.cc`](https://github.com/apache/tvm/blob/v0.14.0/src/tir/usmp/algo/hill_climb.cc)
uses conflict-guided ordering moves with a decaying willingness to accept worse
candidates. Its useful idea is to search a compact ordering state while a
deterministic greedy decoder produces placements.

`tvm_hill_climb` freezes that policy. Our generic `local_search` adds insertion
and reversal moves and deterministic restarts. `pypto_structured_search` keeps
the same decoder abstraction but adds moves whose units are pipeline groups,
semantic alias identities, and expensive reuse edges. This makes TVM an
ablation baseline rather than an undocumented ingredient of the new method.

### MiniMalloc

[MiniMalloc](https://github.com/google/minimalloc) is an exact solver for the
single-pool, fixed-size, single-interval core. It searches canonical solutions,
uses spatial inference for early backtracking, and removes dominated states.
The paper is Michael D. Moffitt,
[*MiniMalloc: A Lightweight Memory Allocator for Hardware-Accelerated Machine
Learning*](https://doi.org/10.1145/3623278.3624752), ASPLOS 2023.

MiniMalloc is our ground truth where its contract applies. A–K are adversarial
one-MiB instances designed to distinguish heuristics from exact search. It is
not a valid direct solver for PyPTO constraints. Per-pool core relaxations are
reported only as lower bounds, and only when the projection is demonstrably
sound.

### TelaMalloc

Maas, Beaugnon, Chauhan, and Ilbeyi's
[*TelaMalloc: Efficient On-Chip Memory Allocation for Production Machine
Learning Accelerators*](https://doi.org/10.1145/3567955.3567961), ASPLOS 2023,
studies production on-chip instances where compilation latency and solution
quality both matter. Its combination of fast heuristics, constraint solving,
and targeted backtracking reinforces two design choices here:

- report a time/attempt budget with every result instead of comparing only
  final height;
- keep an exact/offline oracle separate from a bounded in-compiler heuristic.

TelaMalloc does not provide a drop-in open implementation in this repository,
so comparisons should use published results or a separately documented
reimplementation, never the label alone.

### TensorFlow Lite Micro

TFLite Micro's
[`GreedyMemoryPlanner`](https://github.com/tensorflow/tflite-micro/blob/096563546742ba81adb6f012ab718d196a48e02d/tensorflow/lite/micro/memory_planner/greedy_memory_planner.cc)
is a useful minimal production baseline: order long buffers and greedily search
gaps in one arena. The corresponding
[`header`](https://github.com/tensorflow/tflite-micro/blob/096563546742ba81adb6f012ab718d196a48e02d/tensorflow/lite/micro/memory_planner/greedy_memory_planner.h)
explicitly states that the NP-complete placement problem is not solved
optimally. It demonstrates why a small deterministic allocator remains worth
keeping even after stronger search exists: it is predictable, cheap, and a
reliable fallback.

## What is genuinely PyPTO-specific?

The extra structure should be divided into hard semantics and optimization
preferences.

### Hard constraints

- **Mandatory allocation identity:** views, in-place results, loop carries, and
  branch phis that must share one base allocation.
- **Whole-slot reuse:** current PyPTO/PTOAS dependency tracking permits equal
  bases or disjoint slots, not arbitrary partial overlap of temporally disjoint
  values. This is a temporary but real feasibility constraint.
- **Pipeline separation:** simultaneously active depth/residue copies must not
  collapse into one address merely because a scalar lifetime projection says
  they do not overlap.
- **Target/operation separation:** hardware hazards and explicit no-alias rules.
- **Pool capacity, alignment, reservations, and pins.**
- **Sound physical lifetime:** SSA-reference gaps are not reusable physical
  holes without a separate value-death proof.

These belong in validation and capability matching. No objective weight may
trade them away.

### Objective

Peak bytes alone is insufficient for a compiler scratchpad. Once every pool
fits, reducing peak below capacity can be worthless while manufacturing a
cross-pipe dependency can serialize execution. The current research objective
is therefore lexicographic:

```text
1. capacity overflow       must reach zero
2. reuse/synchronization   minimize predicted costly address reuse
3. total peak              weak tie-break across pools
4. maximum pool peak       final tie-break
```

This avoids arbitrary conversion between bytes and cycles. If measured data
later supports calibrated weights, a new schema version can add weighted or
Pareto aggregation without changing schema-v1 results.

The present `reuse_penalties` are sparse pair costs. They are hypotheses until
correlated with generated PTO events/barriers and device latency. Benchmark
reports must show both raw objective components so a lower scalar score cannot
hide a worse peak or synchronization estimate.

### Structured search state

The first PyPTO-specific solver still decodes an ordering; its distinction is
the neighborhood:

- generic swap, insertion, and reversal;
- pipeline group block relocation;
- semantic-alias identity priority changes;
- reuse-penalty endpoint separation.

This is a controlled intermediate step. The next algorithmic layer should add
direct offset-to-gap moves and bounded local repair, because ordering-only
decoders cannot express every useful placement neighborhood cheaply. Each move
family should be independently disabled in benchmark ablations.

## Scheduling and allocation

PyPTO's `CanonicalizeIOOrder` chooses a legal topological statement order
before lifetime analysis. That order fixes the DSA rectangles, but the current
ordering rule is not an allocator-aware global optimizer. The research problem
is therefore naturally bilevel:

```text
schedule candidate
  -> exact lifetime/constraint export
  -> bounded DSA solve
  -> placement + synchronization estimate
  -> schedule move or accept
```

An integrated formulation is theoretically possible, but starting with a
bounded outer loop has practical advantages: it reuses the independently
validated allocator, preserves compiler legality checks, and allows the same
fixed-schedule corpus to evaluate inner solvers. Candidate schedule moves can
target live-range pressure, pipeline depth, or cross-pipe reuse. Device
performance, not peak alone, must select the eventual co-optimization
objective.

## Evaluation matrix

| Method | Standard DSA | PyPTO direct | Role |
| --- | --- | --- | --- |
| first fit | yes | yes | deterministic fallback |
| XLA spatial best fit | yes | no | frozen compiler heap baseline |
| TVM hill climb | yes | yes | frozen ordering-search baseline |
| generic local search | yes | yes | generic neighborhood ablation |
| MiniMalloc exact | yes | relaxation only | optimum/lower-bound oracle |
| PyPTO structured search | no | yes | proposed structured neighborhood |

The real-instance corpus must report DeepSeek/Qwen source coverage separately
from unique problem shapes. Exact structural duplicates count once in solver
aggregates; trivial shapes remain in the manifest; unique nontrivial shapes are
kept even when they appear easy for every current heuristic.

## Immediate research experiments

1. Run XLA, TVM, generic local search, and MiniMalloc on A–K with equal,
   disclosed budgets.
2. Ingest the full PyPTO-Lib export archive, deduplicate structurally, and
   stratify by buffers, conflict density, reuse candidates, pipeline groups,
   alias width, pools, and capacity pressure.
3. Ablate each PyPTO move family on the same selected corpus.
4. Measure whether predicted reuse cost correlates with PTO event/barrier count
   and warm device latency.
5. Add direct offset-to-gap moves and bounded repair only after the ordering
   baseline tables are reproducible.
6. Explore schedule/allocation outer-loop moves after fixed-schedule correctness
   and performance are stable.

## Initial A--K checkpoint

The checked-in
[`minimalloc-1mib-xla` report](../benchmarks/results/minimalloc-1mib-xla/report.md)
uses three seeds, 2,000 ordering-search candidates per seed, and a 60-second
MiniMalloc budget. None of the heuristics reaches the one-MiB capacity. TVM
hill climb has the best heuristic height on A, B, C, E, F, J, and K; generic
local search leads on D, G, H, and I. The one-shot XLA policy is competitive in
compile time but is not a quality winner on this adversarial set. MiniMalloc
certifies an optimum for eight instances and times out without a certificate
on D, E, and J. These results justify keeping all three heuristic baselines and
developing stronger placement-level neighborhoods rather than tuning one
ordering policy against compiler-specific cases alone.
