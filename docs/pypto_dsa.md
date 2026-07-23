# PyPTO and dynamic storage allocation

## Scope

This document defines PyPTO memory planning on top of standard Dynamic Storage
Allocation (DSA) and tracks PyPTO
[#1980](https://github.com/hw-native-sys/pypto/issues/1980). Operation order,
tiling, pipeline construction, and physical memory spaces are fixed inputs.
The current problem is schedule-aware but does not change those inputs.
Schedule/allocation co-optimization is future work.

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

## Why study the planner in PyPTO?

The relevant compiler order is:

```text
PyPTO logical dependency graph and selected operation order
  -> PyPTO physical placement using abstract access routes
  -> PTOAS lowering to concrete accesses and resources
  -> synchronization induced or validated from physical aliasing
```

The final synchronization set cannot be fixed before placement: assigning two
buffers overlapping addresses may activate a WAR or WAW dependency absent from
the logical graph. Conversely, a schedule chosen assuming distinct storage may
need new waits after reuse. Full scheduling--allocation co-optimization could
search both decisions, but is a larger problem. The current refinement fixes
the nominal order and minimizes a surrogate for the address-induced
dependencies activated by placement. This is schedule-aware memory planning,
not full co-optimization.

PyPTO and PTOAS observe complementary facts:

| PyPTO-level fact | Use in memory planning | Current adapter status |
| --- | --- | --- |
| dependency DAG and the topological order selected by `CanonicalizeIOOrder` | detect absent ordering and eventually explore order/placement together | partial SSA ordering only |
| knowledge inside the load-motion transformation | identify reuse that defeats a deliberately early load | provenance is not yet recorded or exported |
| pipeline group, stage, residue, and requested depth | preserve ping-pong intent or warn when capacity forces relaxation | exported |
| loop, branch, if-phi, and loop-carried semantics | materialize aliases and retain high-level control context | aliases materialized; recognizer support is partial |
| semantic aliases, shapes, tiling, and source locations | enforce correctness and issue programmer-facing diagnostics | partially exported or used by the adapter |

PyPTO already knows operation kinds, source and destination memory classes,
logical dependencies, and structured control flow. These facts motivate a
target-independent route recognizer for potential address-induced WAR/WAW
relations. The recognizer now derives those routes from resolved operand and
result memory spaces rather than an operation-name or `PipeType` allowlist.
PTOAS remains the authoritative correctness layer: it
sees exact lowered accesses, removes redundant dependencies, allocates events,
and emits the final synchronization.

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

## Recognizing address-induced hazards

The portable mechanism is a false physical-memory dependency. Two independent
buffers have disjoint logical lifetimes, but a placement may give them
overlapping byte ranges. A write that starts the later lifetime must then wait
for the earlier buffer's final asynchronous access to finish.

For independent buffers, the later lifetime starts with a write. The relevant
relations are therefore:

| Earlier terminal access | Relation created by reuse |
| --- | --- |
| read | WAR |
| write | WAW |

RAW describes semantic dataflow rather than optional reuse between independent
buffers. Target-specific correctness hazards remain hard separations.

### Abstract access routes

The recognizer classifies **accesses**, not buffers. One buffer can be written
by one route and later read by another. A transfer access is identified by its
source and destination memory classes:

```text
route = (source class, destination class)
```

The portable classes are `external`, `UB`, `L1`, and `L0`. L0A, L0B, and L0C
remain separate physical DSA arenas; they are merged only in this access-route
taxonomy. Compute accesses use abstract `vector` and `matrix` resource classes.

Each route maps to an abstract resource class. Equal classes are tagged
`same_resource`; distinct classes are tagged `cross_resource` and may execute
asynchronously. Route equality is not a happens-before proof: operations on one
resource may still require completion ordering. A target may later map several
routes to one resource or reject unsupported routes without changing the
WAR/WAW construction.

The implemented portable table is:

| Source -> destination | Abstract resource |
| --- | --- |
| `external -> {UB, L1}` | `inbound_dma` |
| `{UB, L1} -> external` | `outbound_dma` |
| `L0 -> external` | `l0_to_external` |
| `L1 -> L0`, `L0 -> L1` | directed local-transfer resource |
| `UB <-> L1`, `UB <-> L0` | directed optional local-transfer resource |
| `UB -> UB` compute | `vector_compute` |
| `L0 -> L0` compute | `matrix_compute` |
| tile/scalar access | `scalar_access` |

Zero-copy view operations add no execution access; their downstream users are
attributed to the shared physical allocation. Every recorded access still
retains the exact PyPTO arena, so merging Left/Right/Acc/Bias into the `L0`
taxonomy does not merge their independent DSA address spaces.

An A2/A3 mapping is useful as a reference, not as the formulation:

| Abstract access | A2/A3 example |
| --- | --- |
| `external -> UB` | MTE2 writes UB |
| `UB -> external` | MTE3 reads UB |
| `external -> L1` | MTE2 writes L1 |
| `L1 -> external` | MTE3 reads L1 |
| `L1 -> L0` | MTE1 writes L0A/L0B |
| `L0 -> L1` | FIX reads L0C and writes L1 |
| `L0 -> external` | target drain path for an accumulator store |
| vector compute | Vector reads/writes UB |
| matrix compute | Matrix reads L0A/L0B and writes L0C |

A2/A3 has no L0-to-UB or L1-to-UB route; A5 may provide additional mappings.
Those target differences refine the route table, not the WAR/WAW construction.

### Candidate construction

For each physical arena, examine every lifetime-compatible pair. Orient the
pair so `A` is the earlier buffer and `B` the later buffer. A single global
last access is insufficient: a late Vector access does not prove that an
earlier MTE read has completed. Instead, construct an access frontier containing
the maximal relevant accesses per resource, control path, loop context, and
known byte range. Each abstract resource is treated as one completion-ordered
issue chain. Real SSA def-use is another completion relation; lexical order
alone is not. Nested accesses are represented by their enclosing structured
statement in each parent region, and ordering is tested in the nearest common
region. Thus a branch-local producer is correctly dominated by a consumer of
the `if` result after the branch. Construct the complete antichain of minimal
accesses for `B` and require every member to be a verified write. For every
compatible frontier pair, record:

```text
maximal read/write of A -> minimal write of B
```

The candidate is a WAR or WAW record tagged `same_resource` or
`cross_resource`. The solver still receives an ordinary weighted pair edge,
activated only when the selected byte ranges overlap. Access routes,
operations, loop context, and ordering evidence are producer provenance; they
do not change the pairwise optimization geometry. Initial promotion policy may
price only cross-resource candidates while retaining same-resource candidates
for reporting and later study.

Same-resource issue order and real SSA def-use are existing completion
dependencies, so their handoffs need no additional reuse edge. Bare source
order is not sufficient. If the recognizer cannot prove that every minimal
access of the later allocation is a write, it retains the record for diagnosis
but does not promote it. A read and write in the same operation are also
separate from inter-operation reuse: their legality comes from the operation's
alias contract and cannot be repaired by inserting a wait.

Loops do not introduce another dependency type. They add context to WAR/WAW:

- `in_loop`: the relation occurs in the loop body and repeats;
- `loop_carried`: the relation crosses the backedge into a later iteration.

The reference recognizer records both body-local distance-zero handoffs and the
distance-one cyclic handoff from the later value in iteration `k` to the earlier
value in iteration `k+1`. It considers every shared enclosing-loop backedge;
opposite branch arms remain exclusive within one iteration but may both execute
across different iterations. Branch and loop identity remain provenance on the
access-site record before records are aggregated into a buffer-pair edge.

Pipeline membership and load-motion provenance express PyPTO performance
intent. They may prioritize candidate edges, but do not independently prove an
address-induced hazard.

### Recognition versus cost

Candidate recognition is mechanical. Final cost is contextual because PTOAS
may suppress an already-covered dependency, coalesce several relations into one
event, substitute a barrier, or exhaust the event-ID budget. Therefore three
questions remain separate:

1. can overlap create a WAR/WAW relation;
2. does PTOAS emit additional final synchronization; and
3. what latency does that synchronization add?

The first question belongs to the recognizer. Fixed-placement PTOAS comparisons
and isolated kernel measurements answer the second and third. Complete
distance-zero cross-resource relations may be promoted even inside structured
control. Same-resource and loop-carried relations remain report-only; loop
context must be validated before it affects promotion or weight.

The optimization model remains additive:

```text
reuse_cost = sum over pair edges e of weight_e * overlap_e
```

Grouped OR triggers, global event budgets, and negative/context-dependent costs
are deferred until evidence requires a more complex formulation.

### Current implementation status

The opt-in `QUADRATIC` implementation is now the coverage-first reference. It
normalizes semantic aliases to physical allocations, preserves per-resource
access frontiers, scans flat and nested regions, uses resource issue order and
SSA def-use to suppress already-completed handoffs, and records distance-zero
and distance-one loop handoffs. It promotes only complete, full-allocation,
distance-zero cross-resource evidence. A partial or unknown byte range, an
uncertain initial-write frontier, a same-operation handoff, and a loop-carried
handoff remain report-only. The earlier adjacent-only linear mode is disabled.
Reference work scales with the product of the two access-frontier sizes for
every lifetime-compatible buffer pair, not merely with the number of pairs;
this cost is accepted only in the opt-in research mode.

The remaining recognition gaps are target validation of the abstract-resource
mapping, precise subrange activation for views, loop-replication promotion, a
complete operation alias-contract table, and splitting abstract resources that
have multiple independently completing channels. Only after the reference
agrees with fixed-placement PTOAS evidence should a faster generator be
derived.

Schema-v1 reason names remain provenance labels. `pipeline_serialization` is
emitted by the pipeline fallback and `cross_pipe` by the opt-in recognizer;
other reason names do not imply implemented producer policies. Event pressure
is global and must not be represented as an ordinary pair reason.

## Evidence so far

The default PyPTO adapter emits `pipeline_serialization` only through the
strict-to-soft fallback. The opt-in recognizer can additionally emit
experimental unit `cross_pipe` edges. Other schema-v1 reason names have no
active PyPTO producer.

Fixed-placement experiments found:

- In a 16-placement gather factorial, four lifetime-compatible overlaps had
  heterogeneous effects: one overlap induced an event, one overlap was
  beneficial because separating it introduced a much more expensive in-loop
  barrier, and two overlaps were free.
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
- Pair isolation traced the five `mlp_block_aiv` barriers to eight of 24 tested
  overlaps; the other 16 were neutral. The `dyn_online_update` and
  `mlp_block_aic` deltas were also traced to concrete buffer pairs.
- The isolated `dyn_online_update` pair `(18,19)` had a mixed raw effect: it
  added an `MTE3 -> V` event while removing a `V -> V` barrier. It cannot yet be
  classified by one positive scalar.
- OR behavior was confirmed only within a synchronization site keyed by
  consumer node. `tadds` and AIC Acc each formed one OR group; `tmul` and AIC
  Right each formed two handoff groups, additive across groups and OR only
  within the shared-consumer group. The earlier interpretation of one OR over
  all three pairs was incorrect. This is evidence about pair-model
  approximation error, not yet a reason to change the formulation.
- AIC overlap `(5,7)` had a stable negative net effect in two surrounding
  placements: it removed a `FIX -> M` sync and changed the loop-carried count
  from 18 to 16. This resolved the earlier accounting discrepancy, but no
  isolated latency result yet justifies a negative weight.
- No experiment established a useful universal weight by reason category.

These results support ordinary pair edges as the simplest starting model, but
also require positive, zero, apparently beneficial, and context-dependent
classifications. A false-positive penalty can select a worse placement.

## Current research method

Start from the pairwise model and escalate only when evidence falsifies it:

1. run the opt-in candidate recognizer over captured PyPTO problems without
   treating its unit promotion policy as calibrated;
2. classify each predicted pair against fixed-placement PTOAS outcomes as true
   positive, false positive, false negative, or true zero;
3. trace mismatches through alias detection, existing-order suppression,
   loop handling, and redundant-sync removal;
4. repeat disputed pairs in a second surrounding placement;
5. retain raw effect vectors `(in-loop barriers, in-loop events,
   straight-line barriers, straight-line events)` rather than prematurely
   assigning scalar weights; and
6. use isolated kernel cycles and device correctness only for informative
   disagreements or candidate negative effects.

Classify each pair as positive, zero, apparently beneficial, context-dependent,
or not isolatable. Promote a fixed positive edge only when its harmful sign and
mechanical explanation are stable. OR groups, hyperedges, retained-depth
factors, global event budgets, and negative weights remain deferred until
repeated evidence justifies the additional optimization complexity.

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
