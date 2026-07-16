# PyPTO and dynamic storage allocation

PyPTO captures compiler-derived DSA instances and may later add measured
performance objectives. Compiler provenance is not, by itself, a new DSA
problem.

## Standard problem

For one memory pool, buffer `i` has size `s_i`, alignment `a_i`, and half-open
lifetime `L_i`. The solver chooses an offset `x_i` and minimizes

```text
H = max_i(x_i + s_i).
```

The placement is valid when every offset is aligned and buffers whose lifetimes
overlap have disjoint address ranges:

```text
x_i mod a_i = 0
L_i intersects L_j  =>  [x_i,x_i+s_i) does not intersect [x_j,x_j+s_j)
```

This is the formulation needed by issue #1908. If an early 64 KiB buffer dies
before two co-live 32 KiB buffers, the later buffers may occupy `[0,32 KiB)` and
`[32 KiB,64 KiB)`. They partially overlap the old buffer at different bases and
the peak remains 64 KiB.

PyPTO has several fixed local-memory pools. With fixed pool assignment and no
cross-pool cost, these are independent DSA instances packaged in one document.
Capacity changes peak minimization into the decision question `H <= C`; it does
not define a new geometry. Likewise, the current uniform 32-byte alignment only
lets an implementation scale sizes and offsets by 32.

## Compiler facts around the standard problem

The adapter exports one physical buffer after mandatory views, in-place values,
branch-phi storage, and loop carries have been materialized. It also exports:

- one conservative physical-lifetime hull per allocation;
- fixed pool, capacity, alignment, and reserved prefix;
- unconditional separation pairs for simultaneous pipeline copies, target
  hazards, and semantic no-alias requirements; and
- alias and pipeline metadata for audit and research.

These facts either preprocess the instance or add ordinary conflicts. They do
not replace standard DSA and do not forbid partial overlap between
lifetime-disjoint buffers. `pypto_hard_v1` is therefore a schema validation
profile for a compiler capture, not a distinct optimization problem.

The lifetime hull is correctness-critical. A former exporter emitted holes in
DeepSeek-v4 loop-carried accumulator lifetimes; a solver legally reused one of
those false holes and corrupted device output. The fix was to export the sound
physical hull, not to weaken DSA placement.

## Pipeline intent already grounded by PR #1949

Pipeline clones are sequential in scalar program order but are intended to run
concurrently on asynchronous hardware units. If two stages share an address,
the reuse creates a false write-after-read dependency: the next prefetch must
wait for the previous consumer, destroying the intended ping-pong overlap.

PR #1949 demonstrates this mechanism in PyPTO. `pipeline_membership=(group,
stage)` identifies the clones. The strict capture exports all requested stages
as hard `pipeline_stage` separations. The compiler first solves that ordinary
capacity-constrained DSA problem.

If the strict search finds no fitting placement, the explicit
`BuildPipelineIntentRelaxation` transformation removes only the
`pipeline_stage` reason. Any target-hazard or semantic reason on the same pair
remains hard. Lifetime-disjoint relaxed pairs receive the experimental reason
`pipeline_serialization`, and the problem is solved with:

```text
(capacity overflow, reuse cost, total peak, max peak)
```

This is a named research relaxation, not behavior that a standard solver may
apply silently. A compiler using it must report that requested pipeline overlap
may be lost.

Formally, if `P` is the set of typed pipeline-stage pairs, the strict problem
adds address disjointness for every pair in `P`. The fallback removes only
those edges and minimizes:

```text
lex(capacity_overflow, sum((i,j) in P) w_ij * reuse(i,j),
    total_peak, max_peak)
```

`reuse(i,j)` is one only for lifetime-disjoint buffers whose placed address
ranges overlap. Ordinary lifetime conflicts and every non-pipeline separation
remain hard.

This separates two questions cleanly:

- **correctness:** overlapping lifetimes and explicit separations may never
  overlap in address;
- **performance:** among otherwise valid standard-DSA placements, some address
  reuse can add dependencies and reduce overlap.

## Actual research refinements

Only a changed feasible set or objective constitutes a DSA refinement. Three
closely related candidates are worth evaluating.

### 1. Pipeline-overlap-aware DSA

Fit within capacity, then minimize cross-stage address reuse that would collapse
known pipeline overlap. The first implementation uses sparse pair costs:

```text
reuse(i,j) = 1 when address ranges overlap and lifetimes are disjoint
minimize sum(c_ij * reuse(i,j))
```

`c_ij` is present only for different stages of the same pipeline group. A hard
version turns selected pairs into separations; a soft version exposes the
memory-versus-overlap tradeoff. PR #1949 grounds the mechanism, but device runs
must calibrate whether unit pair counts predict latency.

### 2. PTOAS-synchronization-aware DSA

More generally, reusing an address across asynchronous producers and consumers
may make PTOAS emit an anti-dependency, event, wait, or barrier. The objective
would minimize the synchronization actually induced by a placement. PyPTO does
not know the final hardware-pipe assignment at export time, so a static
`cross_pipe` label is insufficient. This candidate needs PTOAS instrumentation
or a one-round placement-to-PTOAS feedback path.

### 3. Critical-path and event-budget-aware DSA

Synchronization costs are not necessarily additive. Two individually cheap
reuse edges can form a serial chain, while a reuse edge already implied by the
dependency graph can be free. A stronger evaluator augments the scheduled DAG
with reuse-induced edges and measures critical-path growth. Event identifiers
are a separate scarce resource: palette exhaustion may cause a barrier and is
better modeled as a hard bound or discrete objective term than as another byte
weight.

Candidates 2 and 3 are hypotheses until PTOAS output and device latency confirm
them. Bank residue, multi-interval liveness, flexible pools, and piecewise sizes
remain out of the required profile; field names alone are not sufficient
motivation.

## Objective and experiments

The strict path minimizes peak and validates capacity. The explicit fallback
requests lexicographically:

```text
(capacity overflow, reuse/synchronization cost, total peak, max peak)
```

Raw components must also be reported; arbitrary conversion between bytes and
cycles is not acceptable. Controlled evaluations should compare placements of
the same scheduled program and record:

1. peak and feasibility;
2. emitted PTOAS dependencies, events, waits, and barriers;
3. retained pipeline depth and generated PTO;
4. numerical correctness; and
5. repeated device latency and utilization.

The smallest useful A/B test holds tiling and operation order fixed and changes
only one reuse relation. DeepSeek and Qwen cases should be held out when fitting
any cost model.

## Search

`pypto_structured_search` is a heuristic, not a problem definition. Today it
uses the common standard-DSA placement engine plus generic ordering moves and
pipeline/reuse-guided moves. It should be improved only after the corresponding
objective predicts measured behavior. Until then, standard DSA tables compare
peak and runtime; research tables must report synchronization and device effects
separately.
