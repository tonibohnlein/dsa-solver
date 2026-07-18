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

## Hazard categories and pair-edge construction

The portable mechanism is a false physical-memory dependency. Two logical
buffers may be independent and lifetime-compatible, yet assigning overlapping
addresses makes accesses to one physical range appear dependent. On hardware
with asynchronously issued compute and transfer units, enforcing that new
ordering can reduce concurrency.

A positive edge needs a fixed-placement counterfactual:

```text
same program, schedule, and all unrelated offsets
placement A: pair uses disjoint ranges
placement B: pair overlaps
evidence: final PTOAS synchronization or kernel cycles worsen only in B
```

PTOAS is the current concrete implementation used to discover and test these
hazards. When both local accesses have known physical addresses, it compares
their absolute half-open byte ranges across allocation roots. Otherwise it
falls back to root/view provenance and conservative cases. The general
categories are:

| Category | Portable interpretation | PTOAS manifestation | Pair-edge action |
| --- | --- | --- | --- |
| Cross-resource RAW, WAR, or WAW | Reuse orders accesses issued on different asynchronous resources | set/wait event pair | positive candidate |
| Same-resource RAW, WAR, or WAW | A later issue must wait for completion on the same asynchronous resource | pipe barrier | positive candidate, distinct class |
| Loop-crossing hazard | The reuse dependency recurs across a loop back-edge | retained or multi-event synchronization | positive context modifier |
| Read/read | Sharing does not order two writes or a read against a write | normally no sync; ACC has a target-specific exception | target-specific: hard only if aliasing is illegal, otherwise a positive candidate for required ordering |
| Already ordered | A placement-independent dependency already provides the required happens-before relation | candidate sync suppressed or removed | zero; placement-induced ordering is context-dependent |
| Proven-disjoint rotating slots | The relevant dynamic slots cannot coincide | forward slot-affine disjoint proof | zero |
| Target-specific safe access pattern | A hardware/compiler rule proves the access chain safe | MTE2 load/load WAW exemption, or exact forward PIPE_V chain with repeat at least 16 | zero only for that target and context |
| Observed synchronization substitution | Changing one overlap changes which event or barrier represents several dependencies | net synchronization may decrease; exact pass path remains unresolved | record the raw effect; do not emit a negative edge yet |
| Event-resource pressure | Several dependencies compete for a bounded event palette | coalescing or `PIPE_ALL` fallback | deferred global model |

Therefore `cross_pipe`, raw event counts, and pipeline membership are too coarse
to assign a calibrated production cost. The v1 cross-pipe unit policy below is
only a hypothesis for controlled experiments. Production weights need evidence
from final barrier/event topology, source/destination pipes, control-flow
location, and redundant-sync removal.

The current candidate-generation hypothesis is deliberately sparse. Add a
positive pair edge only when all four statements hold:

```text
the buffers are lifetime-compatible and may share an address
+ their accesses contain a RAW, WAR, or WAW relation if that sharing occurs
+ the execution-resource assignment can leave the accesses concurrently outstanding
+ no existing dependency already provides the required ordering
```

Pipe category, pipeline membership, or lifetime compatibility alone is not
enough. The optimizer still receives an ordinary pair `(buffer_i, buffer_j,
weight)`. Access operations, resources, loop/branch context, and PTOAS output
are producer-side evidence for deciding whether that pair exists and later
choosing its weight. They are not part of the pairwise optimization problem.

An OR-trigger objective would be a different formulation:

```text
pairwise: sum over edges e of w_e * overlap_e
grouped:  sum over groups g of w_g * OR(overlap_e for e in g)
```

The grouped form is non-separable and requires group activation variables. It
is deferred even though PTOAS sometimes lets several overlaps trigger one sync,
because the simpler pairwise approximation may be sufficient.

### Can PyPTO recognize the edges cheaply?

PyPTO has useful source facts before DSA export:

- statement order and conservative allocation lifetimes;
- physical allocation identities and SSA definitions/uses;
- loop, branch, core, and surviving pipeline membership; and
- explicit semantic and target-hazard separations.

The first PyPTO recognizer deliberately derives only a high-confidence subset;
`AllocationPlan` still does not retain general access frontiers, sub-access
regions, execution resources, or a happens-before summary. Those facts are only
partly derivable from the current IR:

- `DeriveCallDirections` runs after memory allocation and intentionally skips
  builtin `tile.*` operations, so it cannot supply tile memory effects here.
- `Backend::InferPipe` exists, but no backend currently registers per-op pipe
  callbacks; its fallback `S`/`V` classification is not a PTOAS resource model.
- `BuildStmtDependencyGraph` provides direct SSA def-use edges per region, not
  transitive reachability, loop-backedge ordering, or already-inserted sync.
- one PyPTO call can lower to several PTO operations with different resources
  and sub-accesses.

Exact PTOAS-equivalent recognition is therefore not currently available. It
would require a tile-operation memory-effect description, a one-to-many
lowering/resource description, access-region information, and a stronger order
analysis.

The implemented candidate recognizer has two opt-in modes and is disabled by
default:

1. use an allowlist whose direct tile arguments and result provide provisional
   full-allocation read/write effects and an execution-resource class;
2. collect per-allocation access endpoints and structured-region context in one
   traversal, retaining whether the terminal access is a read or write;
3. consider only an earlier terminal access and later initial write that are
   adjacent in the same flat `SeqStmts` region and have no direct SSA edge;
4. classify the physical handoff as WAR or WAW and same-pipe or cross-pipe; and
5. apply a separate experimental v1 policy that promotes cross-pipe candidates
   to unit positive edges while leaving same-pipe candidates report-only.

The allowlist and SSA ordering are a candidate-generation hypothesis, not a
proof that PTOAS inserts additional synchronization. They do not model
one-to-many lowering, pre-existing memory ordering, synchronization
substitution, or redundant-sync removal. Those limitations are why the feature
is opt-in and why candidate counts are reported separately from promoted edges.

With a constant-size table of incompatible endpoint signatures, terminal
accesses can be bucketed by `(flat_region, statement_index, signature)`. An
initial write queries only incompatible signatures at its immediately preceding
statement index. This subset can target `O(A + B log B + H)`, where `A` is
access count, `B` is buffer count, and `H` is the adjacent candidate pairs
actually enumerated. Ambiguous control flow, unclassified or multi-operation
lowering, partial regions, and pairs needing a transitive order proof
remain at zero. This is exposed as `DsaReusePenaltyRecognizer.LINEAR` and is the
only mode that satisfies PyPTO's production pass-complexity bound.

`DsaReusePenaltyRecognizer.QUADRATIC` is an explicitly experimental reference.
It scans all lifetime-compatible pairs within each supported simple region,
including nested regions, and uses transitive intra-region SSA reachability to
discard already-ordered pairs. Nested candidates are reported but are not
promoted by the v1 unit policy. Its
pair scan is `Theta(B^2)` and its cached ancestor construction can add
`O(B(N+E))`; it must not become the default compiler path. Comparing its edge
set with `LINEAR` measures the coverage sacrificed for linear scaling.

Schema v1 activates a pair cost on any byte overlap, whereas PTOAS hazards can
depend on which subranges overlap. Promote an ordinary pair edge only when the
hazardous accesses cover the allocation sufficiently that every relevant legal
overlap has the same classification. Otherwise mark the observation as
geometry-dependent and omit it from the first recognizer.

The relevant PTOAS implementation is under:

```text
PTOAS/lib/PTO/Transforms/InsertSync/
  MemoryDependentAnalyzer.cpp
  InsertSyncAnalysis.cpp
  RemoveRedundantSync.cpp
  SyncEventIdAllocation.cpp
```

As of upstream PTOAS `79463ac8`, this pipeline builds per-operation read/write
memory information, checks physical aliasing, detects RAW/WAR/WAW relations,
suppresses already-covered dependencies, inserts same-pipe barriers or
cross-pipe events, handles loop back-edges and event IDs, and removes redundant
synchronization. PTOAS PR #948 adds broader explicit-address provenance and
mixed/inexact-subview handling but is not yet part of that upstream revision.

The fixed-placement reports below used PTOAS `007f2d63`: PR #948 plus
sync-summary and debug instrumentation. Upstream `79463ac8` shares the core
hazard-insertion and redundancy mechanics, but not all explicit-address
provenance or reporting used by the experiments.

## Evidence so far

The current PyPTO adapter emits one live penalty family:
`pipeline_serialization` from the strict-to-soft fallback. Other schema-v1
reason names are experimental vocabulary, not independent producer policies.

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
