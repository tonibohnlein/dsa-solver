# PyPTO and dynamic storage allocation

## Scope

This document defines PyPTO memory planning on top of standard Dynamic Storage
Allocation (DSA) and tracks PyPTO
[#1980](https://github.com/hw-native-sys/pypto/issues/1980). Operation order,
tiling, pipeline construction, and physical memory spaces are fixed inputs.
Schedule/allocation co-optimization is outside the current problem.

## Standard DSA

Consider one contiguous memory arena. Each buffer `i` has positive size `s_i`
and a non-empty half-open lifetime `L_i = [begin_i, end_i)`. The solver chooses
a non-negative integer start position `p_i`; the buffer occupies:

```text
M_i(p) = [p_i, p_i + s_i)
```

If `L_i` and `L_j` overlap in time, `M_i(p)` and `M_j(p)` must be disjoint. The
required arena size is:

```text
H(p) = max over buffers i of (p_i + s_i)
```

Peak minimization chooses a valid placement minimizing `H`. The capacity form
asks whether a valid placement with `H(p) <= C` exists. Time is already encoded
by the lifetime intervals; `H` measures the contiguous address range, including
unused gaps below its highest occupied byte.

## PyPTO constructs standard DSA instances

UB, L1, L0A, L0B, and L0C are independent address spaces and therefore
independent DSA problems. Schema v1 may bundle them in one document, but no
constraint or cost crosses an arena boundary.

For each arena PyPTO adds compiler facts:

- capacity;
- byte alignment (`p_i mod a_i = 0`);
- reserved address ranges;
- mandatory aliases materialized as physical buffers;
- hard keep-apart relations for correctness or pipeline intent; and
- alias/pipeline provenance for validation and research.

These facts construct or constrain an instance; they do not replace standard
DSA geometry. Lifetime-disjoint buffers may overlap partially at arbitrary
aligned positions.

The current adapter path is:

```text
InitMemRef
  -> materialize semantic aliases and writebacks
  -> collect physical buffers and conservative lifetimes
  -> solve and independently validate
  -> write byte offsets to MemRefs
```

Each exported physical buffer uses one conservative lifetime hull. The exporter
must not create unproven holes from member-level SSA ranges: an earlier false
hole allowed a legal-looking placement to corrupt DeepSeek-v4 loop-carried
accumulators.

PTOAS must also validate or synchronize explicit physical-address overlap when
lowering to asynchronous hardware pipes. PyPTO's lifetime and cost models do
not replace that downstream correctness check.

## Capacity-constrained DSA with reuse penalties

Some lifetime-compatible buffers are safe to overlap but introduce additional
synchronization. Schema v1 represents this preference with ordinary weighted
pair edges.

For sparse edge set `E`, edge `e = (i, j, w_e)` is active when the two placed
byte ranges overlap:

```text
I_e(p) = 1 if M_i(p) overlaps M_j(p)
         0 otherwise

reuse_cost(p) = sum over e in E of w_e * I_e(p)
```

Any byte overlap activates the full edge. The solver treats every reason label
as provenance; how a producer chooses edges and weights is a separate modeling
question.

For capacity `C`, the PyPTO refinement is:

```text
find a valid standard-DSA placement p with H(p) <= C
that minimizes reuse_cost(p)
```

Capacity is hard. Penalties rank fitting placements and cannot legalize a
lifetime conflict, reserved-range overlap, misalignment, or hard separation.
Peak may be a final tie-break, but bytes are not converted to cycles with an
arbitrary scalar weight.

## Hard constraints and pipeline fallback

Correctness separations such as `semantic_no_alias` and `target_hazard` always
remain hard.

Pipeline stages are different: distinct storage is performance intent. PyPTO
first represents different stages of one group as hard separations so requested
ping-pong overlap is preserved. If the bounded strict search finds no fitting
placement, `BuildPipelineIntentRelaxation`:

1. removes only the `pipeline_stage` reason;
2. preserves any other reason on the same pair;
3. adds a `pipeline_serialization` pair cost when the pair becomes fully
   relaxable; and
4. selects the fit-then-minimize-reuse objective.

PyPTO warns when the selected placement violates strict pipeline intent. If the
relaxed problem also has no fitting placement, compilation reports
out-of-memory. Because the strict solver is heuristic, failure to find a strict
placement is not a proof of infeasibility; the final solution is checked against
the strict problem before warning.

Unit cost per relaxed pipeline pair is reproducible but uncalibrated. It can
overcount a large group and pipeline membership alone does not prove that reuse
adds synchronization.

## Grounding pair costs in PTOAS

A positive edge needs a fixed-placement counterfactual:

```text
same program, schedule, and all unrelated offsets
placement A: pair uses disjoint ranges
placement B: pair overlaps
evidence: final PTOAS synchronization or kernel cycles worsen only in B
```

PTOAS compares absolute half-open byte ranges, including partial overlaps across
different allocation roots. Address hazards can produce:

| Placement-induced relation | Possible PTOAS result |
| --- | --- |
| Cross-pipe RAW, WAR, or WAW | set/wait event pair |
| Same-pipe RAW, WAR, or WAW | pipe barrier |
| Cross-pipe ACC read/read | target-specific conservative hazard |
| Already-covered dependency | redundant sync removed |
| Proven MTE2 load/load or exact PIPE_V exemption | no new sync |
| Loop back-edge | retained, possibly multi-event synchronization |
| Event-ID pressure | event coalescing or `PIPE_ALL` fallback |

Therefore `cross_pipe`, raw event counts, and pipeline membership are too coarse
to assign a cost. Evidence must include the final barrier/event topology,
source/destination pipes, control-flow location, and redundant-sync removal.

The current candidate-generation hypothesis is deliberately sparse. Add a
positive pair edge only when all four statements hold:

```text
the two physical byte ranges overlap
+ PTOAS finds an access dependence through those ranges
+ the access order and pipe assignment require additional synchronization
+ no existing dependency already provides that ordering
```

Pipe category, pipeline membership, or lifetime compatibility alone is not
enough. The edge records the two buffers whose overlap activates the hazard;
the associated operations, pipes, loop/branch context, and resulting sync group
are evidence used to generate and later weight that edge. Weight calibration is
a separate step.

The relevant PTOAS implementation is under:

```text
PTOAS/lib/PTO/Transforms/InsertSync/
  MemoryDependentAnalyzer.cpp
  InsertSyncAnalysis.cpp
  RemoveRedundantSync.cpp
  SyncEventIdAllocation.cpp
```

## Evidence so far

The current PyPTO adapter emits one live penalty family:
`pipeline_serialization` from the strict-to-soft fallback. Other schema-v1
reason names are experimental vocabulary, not independent producer policies.

Fixed-placement experiments found:

- In a 16-placement gather factorial, four lifetime-compatible overlaps had
  heterogeneous but additive effects: one overlap induced a small costly event,
  one overlap was beneficial because separating it introduced a much more
  expensive in-loop barrier, and two overlaps were free.
- Removing the first event reduced simulator cycles by about 0.7%; separating
  the beneficial overlap increased cycles by about 27%. Synchronization-group
  count alone therefore had the wrong ordering.
- A two/three/four-slot L0A pipeline ladder produced identical final
  synchronization. Those pipeline-derived pair penalties were false positives
  for that kernel.
- A later compact-versus-sparse study held the generated program fixed and
  found four real kernels where compact placement added synchronization. The
  strongest case, `mlp_block_aiv`, added five in-loop `PIPE_V -> PIPE_V`
  barriers and one event. `dyn_online_update`, `mlp_block_aic`, and a pipelined
  matmul added smaller event sets.
- Most compact reuse remained free: removing 67 reuse pairs from
  `mlp_block_aiv` changed only six sync groups, while removing 56 L0A reuse
  pairs from the pipeline case changed none attributable to L0A. Generic
  minimum-aliasing and blanket pipeline penalties therefore over-penalize.
- Gather remained a negative control: removing reuse introduced an in-loop
  barrier. A positive penalty on that overlap could select the slower
  placement.
- No experiment demonstrated a capped OR cost, an all-members hyperedge, or a
  useful universal weight by reason category.

These results support ordinary pair edges as the simplest starting model, but
also require zero, positive, beneficial, and context-dependent classifications.
A false-positive penalty can make a placement slower.

One accounting discrepancy remains open: the compact-versus-sparse report
described loop-carried group counts as unchanged, but the raw
`mlp_block_aic` summaries record 16 versus 18. That delta must be attributed
before drawing conclusions about loop-carried costs.

## Current research method

Start from the pairwise model and escalate only when evidence falsifies it:

1. use `mlp_block_aiv`, `dyn_online_update`, and `mlp_block_aic` as the first
   positive case studies;
2. map each changed PTOAS sync group back to candidate buffer pairs;
3. activate exactly one overlap while every unrelated buffer offset stays
   fixed, and require the overlap-set difference to contain exactly that pair;
4. compare the complete sync topology, not only aggregate group count;
5. repeat the same pair in a second surrounding placement when possible;
6. generate additional fitting loose placements guided by PTOAS mechanisms:
   separate suspected loop barriers and cross-pipe hazards while retaining
   overlaps that are already ordered or exempt;
7. report the nondominated landscape in `(peak, in-loop barriers, in-loop
   events, straight-line events)` rather than collapsing it to one scalar;
8. run one 2x2 interaction test on two independently harmful pairs; and
9. validate informative endpoints numerically and compare in-core cycles.

Classify each pair as positive, zero, beneficial, context-dependent, or not
isolatable. Promote a fixed positive edge only when its harmful sign is stable.
Retain the additive pair objective only if the 2x2 response is additive; OR
groups, hyperedges, retained-depth factors, and global event budgets remain
deferred until a repeated non-additive experiment requires them.

The loose-placement landscape is exploratory evidence, not an algorithm
benchmark. Placements should be constructed from the same fingerprinted
problem with deterministic offset edits. PTOAS rules may guide which overlaps
to remove or retain, but PTOAS synchronization counts are not yet a solver
objective and are not converted into weights during this phase.

End-to-end latency is the final performance criterion, but isolated kernel
cycles are the practical first signal. Device correctness is mandatory;
orchestration wall time is not kernel timing.

## Solver versus formulation

`pypto_structured_search` is one heuristic. It uses the shared DSA placement
engine and adds pipeline-block, alias, and penalty-guided ordering moves. The
problem itself is defined only by buffers, hard constraints, and objective
terms. Unsupported required features return `kUnsupported`; they are never
silently dropped.

Schema profiles and replay rules are defined in
[`structured_problem_schema_v1.md`](structured_problem_schema_v1.md). The
standalone dependency is temporary: after selecting and validating a heuristic,
it can be ported into PyPTO.

## References

- PyPTO:
  [#1908](https://github.com/hw-native-sys/pypto/issues/1908),
  [#1934](https://github.com/hw-native-sys/pypto/pull/1934),
  [#1949](https://github.com/hw-native-sys/pypto/pull/1949), and
  [#1980](https://github.com/hw-native-sys/pypto/issues/1980).
- PTOAS:
  [#913](https://github.com/hw-native-sys/PTOAS/issues/913) and
  [#948](https://github.com/hw-native-sys/PTOAS/pull/948).
- Standard baselines: MiniMalloc, TelaMalloc, TVM USMP, and OpenXLA heap
  simulation; see [`literature_review.md`](literature_review.md).
