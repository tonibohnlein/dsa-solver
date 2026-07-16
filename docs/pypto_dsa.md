# PyPTO and dynamic storage allocation

## Status and scope

Draft. Tracks PyPTO issues
[#1980](https://github.com/hw-native-sys/pypto/issues/1980) and
[#1908](https://github.com/hw-native-sys/pypto/issues/1908).

This document is the authoritative formulation of PyPTO memory planning on top
of standard Dynamic Storage Allocation (DSA). It distinguishes:

- the standard packing problem needed to fix PyPTO #1908;
- compiler facts that only construct or constrain an instance;
- the implemented pipeline-intent refinement;
- proposed synchronization-aware objectives that still need device evidence.

The operation order, tiling, and pipeline construction are fixed inputs. Joint
schedule-memory optimization is outside the current formulation.

## Standard DSA

Consider one contiguous memory arena beginning at position zero. The input is a
set of buffers. Each buffer `i` has:

- a positive size `s_i`, measured in memory units; and
- a non-empty half-open lifetime `L_i = [begin_i, end_i)`.

The solver chooses a non-negative integer starting position `p_i` for each
buffer. While buffer `i` is alive, it occupies the memory interval:

```text
M_i(p) = [p_i, p_i + s_i)
```

Two buffers that are alive at the same time must occupy disjoint memory
intervals:

```text
if L_i and L_j overlap in time,
then M_i(p) and M_j(p) must not overlap in memory
```

Time is therefore already encoded in the lifetime intervals. It determines
which pairs constrain one another; it does not need to appear again in the
spatial objective.

The required arena size is the highest end position of any placed buffer:

```text
H(p) = max over buffers i of (p_i + s_i)
```

The standard DSA optimization problem is:

```text
choose one non-negative start position per buffer
such that simultaneously live buffers do not overlap in memory
and minimize the required contiguous arena size H
```

This measures the size of the contiguous arena `[0, H(p))`. It is not the sum
of buffer sizes: unused gaps below the highest end position also occupy arena
address space.

The capacity decision form asks whether a feasible placement with `H(p) <= C`
exists for a given arena size `C`. Peak minimization and capacity fitting are
two forms of the same standard DSA problem.

## PyPTO instance construction is not a new DSA problem

PyPTO first partitions physical buffers by memory space. UB, L1, L0A, L0B,
L0C, and other physically independent address spaces become separate DSA
instances. A solver invocation therefore sees one arena only; buffers from
different memory spaces cannot conflict, reuse one another, or activate a
reuse penalty.

PyPTO then supplies compiler facts for that arena:

- Capacity selects the fit-within-capacity decision form.
- An alignment requirement `a_i` restricts the starting position to
  `p_i mod a_i = 0`. In PyPTO, sizes and positions are bytes, so 32-byte
  alignment permits starts `0, 32, 64, ...`. The current solver retains byte
  units and rounds candidate starting positions up to the required alignment.
  It does not round buffer sizes or normalize the problem before solving.
- A reserved prefix or range removes address space from ordinary placement.
- A mandatory alias materializes several semantic values as one physical
  buffer.
- An explicit separation adds another conflict edge for a correctness or intent
  reason.
- Pipeline and alias provenance support validation, reporting, and structured
  search.

The solver's `p_i` is a position relative to the beginning of this arena.
PyPTO later writes the corresponding byte address into the MemRef.
Lifetime-disjoint buffers may overlap partially at arbitrary aligned positions.

An implementation may normalize a uniformly aligned instance to smaller
integer units. For a common unit `g`, if every buffer size, capacity, reserved
boundary, and fixed address is divisible by `g`, define:

```text
q_i = p_i / g
u_i = s_i / g
```

The normalized problem is exact, and its peak in bytes is:

```text
H_bytes = g * max over buffers i of (q_i + u_i)
```

The checked-in PyPTO corpus currently satisfies this condition for `g = 32`.
If a size is not divisible by `g`, replacing it with `ceil(s_i / g)` pads the
allocation and changes the problem unless the compiler or architecture already
requires that size padding.

The adapter pipeline is:

```text
InitMemRef
  -> MaterializeSemanticAliases
  -> collect unmerged physical buffers
  -> standalone standard-DSA solver
  -> independent validation
  -> write the chosen byte addresses to MemRefs
```

The lifetime of each exported physical allocation is a conservative hull. The
exporter must not invent holes by unioning member-level SSA ranges. A false hole
previously allowed legal-looking reuse that corrupted DeepSeek-v4 loop-carried
accumulators on device.

PTOAS must independently make explicit physical-address overlap safe when it
inserts low-level synchronization. PyPTO lifetime and cost models do not replace
that downstream correctness check.

## The actual PyPTO refinement: costly physical reuse

Some lifetime-disjoint buffers are legally reusable under standard DSA, but
placing them in overlapping physical ranges can introduce synchronization and
reduce asynchronous overlap.

The optional cost model contains sparse records:

```text
(first buffer, second buffer, integer cost, reason)
```

For penalty edge `e = (i, j, w_e, reason_e)`, define:

```text
I_e(p) = 1  if:
                their lifetimes permit reuse, and
                their placed address ranges overlap
         0  otherwise
```

The current additive reuse cost is:

```text
reuse_cost(p) = sum over penalty edges e of w_e * I_e(p)

a pair activates when:
  - they are allowed to reuse memory temporally
  - their physical byte ranges overlap at all
```

Any overlap activates the full pair cost. Equal-base reuse and partial overlap
are treated identically:

```text
A = [0, 32 KiB), B = [32 KiB, 40 KiB)  -> no overlap, cost 0
A = [0, 32 KiB), B = [24 KiB, 32 KiB)  -> partial overlap, full pair cost
A = [0, 32 KiB), B = [0, 8 KiB)        -> equal base, full pair cost
```

The cost changes preference, not correctness. It cannot legalize lifetime
conflicts, reserved-range overlap, misalignment, or another hard separation.

### Grounded placement effects

A penalty should not be attached merely because two operations use different
pipes. It should represent a downstream synchronization change caused by
overlapping the two physical ranges. PTOAS provides the counterfactual:

```text
compile with disjoint ranges
compile with overlapping ranges
penalty evidence = synchronization present only in the overlapping placement
```

PTOAS checks address-aliasing RAW, WAR, and WAW hazards. A placement-induced
hazard becomes a same-pipe barrier or a cross-pipe set/wait pair. ACC also has a
target-specific cross-pipe read/read hazard. This gives stronger evidence than
the coarse schema-v1 names:

- `induced_set_wait`: overlap adds a cross-pipe event dependency;
- `induced_pipe_barrier`: overlap adds a same-pipe barrier;
- `loop_amplified_sync`: the dependency remains inside a loop and executes each
  iteration;
- `pipeline_depth_loss`: overlap serializes stages intended to run together;
- `event_degradation`: added synchronization exhausts event IDs, reducing
  per-slot events to one event or falling back to `PIPE_ALL`.

The first four can motivate pair or group penalties. Event degradation is
non-additive and should be scored separately. These names describe proposed
measurements; schema v1 currently has only coarser reason codes.

#### Pipeline intent: hard first, then penalized

Different requested stages of one pipeline first receive a hard separation.
Only if no capacity-fitting placement is found does PyPTO relax that intent,
attach `pipeline_serialization` penalties, and emit `PH-DSA-001` when the
selected placement shares a range:

```text
stage 0: load operand_0 -> compute_0
stage 1: load operand_1 -> compute_1

separate ranges: load_1 can overlap compute_0
shared range:    load_1 waits for compute_0; ping-pong becomes serial
```

PyPTO [PR #1949](https://github.com/hw-native-sys/pypto/pull/1949)
demonstrated this false WAR on L0B operands and measured the synchronization
stall on 910B2.

#### General address-induced synchronization

Pipeline membership is not required. Any lifetime-disjoint buffers can create a
new physical-address dependency:

```text
Vector: final read of A
MTE2:   overwrite B

disjoint ranges: no address dependency
overlapping ranges: PTOAS emits Vector -> MTE2 set/wait
```

The same test applies to MTE, Vector, Cube, and same-pipe pairs. A coarse
`cross_pipe` edge should therefore be replaced or calibrated by the actual
PTOAS delta, including source pipe, destination pipe, barrier/event kind, and
control-flow location.

#### Loop and pipeline amplification

A synchronization pair outside a loop may execute once. A loop-carried
dependency remains in the loop, may execute every iteration, and may require
one event ID per rotating buffer slot. The same pairwise reuse can therefore be
cheap in straight-line code but expensive in a hot pipelined loop:

```text
iteration k:     Vector reads stage[k % 2]
iteration k + 1: MTE overwrites the reused address

two ranges: independent rotating stages
one range:  per-iteration set/wait and lost overlap
```

Trip count, loop depth, and retained physical stage count are stronger features
than a unit pair cost.

#### Event-ID and control-flow escalation

PTOAS allocates a finite event-ID pool per pipe pair. Under pressure it can
reduce a multi-buffer dependency to one event; if allocation still fails, it
replaces the dependency with a local `PIPE_ALL` barrier. This discontinuity
cannot be modeled by summing independent pair weights.

PTOAS also removes synchronization conservatively: a branch-spanning sync is
redundant only when both branches cover it, loop-internal sync cannot prove an
outer dependency redundant because the loop may execute zero times, and
compensation sync is retained. Avoiding the placement-induced alias can
therefore remove more synchronization than the initial pair alone suggests.

#### Labels that are not independent mechanisms

`CanonicalizeIOOrder` can mark an early load whose overlap was deliberate, but
`load_motion_serialization` is not a second cost: the actual effect is the
barrier or set/wait caused by address reuse. Likewise, `cross_core` is not yet a
local-arena penalty on either A2A3 or A5; it needs a concrete same-arena
placement choice that changes emitted cross-core synchronization.

`generic` remains useful only for solver tests and external experiments.

## Implemented strict-then-soft pipeline policy

`pl.pipeline(stage=F)` and compiler-created pipelines express an intent to keep
several stages active concurrently. Their scalar statement lifetimes can be
disjoint even though asynchronous execution is intended to overlap.

PyPTO [PR #1949](https://github.com/hw-native-sys/pypto/pull/1949)
demonstrated the mechanism: sharing an operand address makes the next load wait
until the previous compute stops reading that address. The resulting false
write-after-read dependency serializes the pipeline.

### Stage A: preserve requested depth as a hard constraint

PyPTO retains `(pipeline group, stage)` membership. For every different-stage
pair in the same group, the strict problem adds:

```text
[p_i, p_i + s_i) must not overlap [p_j, p_j + s_j)
```

The compiler tries deterministic first-fit and then bounded structured search.
If either finds a capacity-fitting placement, the hard intent is preserved and
no performance warning is emitted.

### Stage B: relax only pipeline intent after strict search fails

If strict search finds no fitting placement:

1. remove only the `pipeline_stage` reason from affected pairs;
2. retain every correctness reason on the same pair;
3. add unit `pipeline_serialization` penalties to fully relaxed,
   lifetime-disjoint pairs;
4. solve the relaxed problem;
5. emit performance hint `PH-DSA-001` if the chosen placement violates the
   strict pipeline separations.

For example:

```text
reasons before: [pipeline_stage, target_hazard]
reasons after:  [target_hazard]
result:         pair remains hard-separated
```

The current solver is heuristic, so “strict search failed” is not a proof of
strict infeasibility. A relaxed solution is therefore revalidated against the
strict problem. If it is also strict-valid, PyPTO keeps the strict profile and
does not warn.

Version 1 assigns unit cost to every relaxed stage-member pair:

```text
pipeline_cost(p) = sum over pipeline pairs (i, j) in P of I_ij(p)
```

This is simple and reproducible, but a group with more member pairs can receive
more cost without losing more effective depth. It is a baseline model, not a
calibrated latency prediction.

## Capacity-constrained objective

For an architecture with arena capacity `C`, capacity is a hard constraint:

```text
find a standard-DSA placement p with H(p) <= C
and minimize reuse_cost(p) among those fitting placements
```

An over-capacity placement is not a worse solution; it is infeasible. Arena size
and raw synchronization components are still reported, but bytes are not mixed
with synchronization using an arbitrary scalar weight. A search algorithm may
use overflow internally to move toward feasibility without making overflow part
of the accepted-solution objective.

The strict pipeline problem is solved first. If it has no fitting placement,
only pipeline-intent separations are relaxed, the compiler emits a performance
warning, and the relaxed capacity-constrained problem minimizes the resulting
penalty. If that problem also has no fitting placement, compilation reports
out-of-memory.

## Candidate refinements to evaluate

These are separate experimental formulations, not one growing production
profile.

### Candidate 1: pairwise pipeline serialization

This is the implemented version:

```text
fit first
then minimize the number or weighted sum of reused cross-stage pairs
```

Evidence: PyPTO PR #1949 proves the false-WAR mechanism. Unknown: whether pair
count predicts latency across pipeline groups and workloads.

### Candidate 2: group-level retained pipeline depth

For pipeline group `g`, let:

- `D_g` be requested depth;
- `F_g(p)` be the number of physically independent stage residues retained by
  placement `p`;
- `w_g` be a measured group importance.

A candidate group cost is:

```text
depth_cost(p) = sum over groups g of w_g * (D_g - F_g(p))

penalize lost pipeline depth per group, not every overlapping member pair
```

This avoids pair-count bias, but requires a precise definition of physical
residue equivalence and device calibration of `w_g`.

### Candidate 3: PTOAS feedback score

Compile candidate placements through PTOAS and compare them with a disjoint
reference placement. Record the induced set/waits, barriers, loop frequency,
retained event multiplicity, and `PIPE_ALL` fallbacks. A first proxy can sum
measured event costs; a stronger evaluator measures critical-path growth.

This handles transitive elimination and event pressure that independent pair
weights miss. PTOAS
[PR #948](https://github.com/hw-native-sys/PTOAS/pull/948) makes explicit
physical overlap visible to downstream correctness analysis; instrumentation is
still needed to expose the performance delta.

## Search is not the problem definition

`pypto_structured_search` is one heuristic for the formulations above. It uses
the common standard-DSA placement engine and adds generic ordering,
pipeline-group, alias, and penalty-guided moves.

The mathematical problem is defined by buffers, hard constraints, and objective
terms. A solver that does not advertise support for a required feature returns
`kUnsupported`; no feature or objective is silently dropped.

Solver improvements should follow evidence that the corresponding objective
predicts device behavior. Standard benchmark tables continue to compare peak
and solver runtime. Research tables must additionally report synchronization
and end-to-end effects.

## Profiles and exported documents

- `standard_dsa` is a literature-compatible DSA problem.
- `pypto_hard_v1` is standard DSA plus validated compiler-derived hard
  constraints and provenance.
- `pypto_research_v1` is a PyPTO capture with an explicit experimental cost or
  relaxation.

Profiles describe schema contracts, not separate geometries. In particular,
`pypto_hard_v1` retains the standard DSA placement geometry.

The external solver dependency is temporary. Once an algorithm and objective
are selected and validated, the heuristic can be ported into PyPTO and the
dependency removed.

## Device-calibration plan

Controlled experiments must hold the scheduled program, tiling, and codegen
inputs fixed while changing only the placement policy.

First run an offline PTOAS A/B for each candidate reuse:

```text
same PTO program and schedule
placement A: selected pair uses disjoint ranges
placement B: selected pair overlaps
compare emitted barriers, set/waits, event multiplicity, and PIPE_ALL
```

Then run controlled device A/B tests for MTE-to-Vector, MTE-to-Cube, same-pipe,
loop-carried, depth-2 pipeline, and event-pressure cases. Record:

1. capacity feasibility and arena size;
2. activated penalties by reason and pipeline group;
3. emitted PTOAS dependencies, events, waits, and barriers;
4. retained physical pipeline depth;
5. numerical correctness;
6. repeated device latency and utilization.

Required comparisons:

- strict pipeline separation versus the relaxed placement;
- pairwise penalty versus group-level lost-depth penalty;
- predicted pair/group cost versus PTOAS-measured synchronization;
- fitted kernels versus held-out DeepSeek and Qwen kernels.

Do not promote a candidate because it improves a proxy alone. It must predict or
improve end-to-end behavior without compromising correctness.

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
  simulation.
