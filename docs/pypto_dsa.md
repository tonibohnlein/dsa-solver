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

Consider one memory pool. Each buffer `i` has:

- size `s_i`;
- alignment `a_i`;
- a conservative half-open lifetime `L_i = [begin_i, end_i)`;
- a chosen byte offset `x_i`.

The memory peak is:

$$
H = \max_i (x_i + s_i).
$$

Plain-text equivalent:

```text
peak = maximum end address of any placed buffer
```

A placement is valid when:

$$
x_i \bmod a_i = 0
$$

and every temporally conflicting pair has disjoint physical ranges:

$$
L_i \cap L_j \ne \varnothing
\Longrightarrow
[x_i, x_i+s_i) \cap [x_j, x_j+s_j) = \varnothing.
$$

Plain-text equivalent:

```text
aligned offset for every buffer
and
overlapping lifetimes imply non-overlapping address ranges
```

The optimization form minimizes `H`. The capacity form asks whether `H <= C`
for pool capacity `C`. These are two views of the same geometry.

### Why this fixes PyPTO #1908

An early 64 KiB buffer dies before two co-live 32 KiB buffers. Standard DSA may
place the later buffers in the two halves of the expired region:

```text
early: [0, 64 KiB)
late0: [0, 32 KiB)
late1: [32 KiB, 64 KiB)
peak:  64 KiB
```

This requires arbitrary partial reuse at different bases. Any model that
partitions buffers into indivisible whole slots recreates the PyPTO #1908
defect.

## PyPTO instance construction is not a new DSA problem

PyPTO supplies compiler facts around the standard problem:

- A fixed memory pool creates one independent DSA problem per pool.
- Capacity selects the fit-within-capacity decision form.
- Uniform alignment discretizes valid offsets but does not change the geometry.
- A reserved prefix or range removes address space from ordinary placement.
- A mandatory alias materializes several semantic values as one physical
  buffer.
- An explicit separation adds another conflict edge for a correctness or intent
  reason.
- Pipeline and alias provenance support validation, reporting, and structured
  search.

None of these facts introduces whole-slot reuse. Lifetime-disjoint buffers may
still overlap partially at arbitrary aligned offsets.

The adapter pipeline is:

```text
InitMemRef
  -> MaterializeSemanticAliases
  -> collect unmerged physical buffers
  -> standalone standard-DSA solver
  -> independent validation
  -> write offsets to MemRefs
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

$$
I_e(x) =
\begin{cases}
1 & \text{if the buffers are in the same pool, their lifetimes permit reuse,}\\
  & \text{and their placed address ranges overlap,}\\
0 & \text{otherwise.}
\end{cases}
$$

The current additive reuse cost is:

$$
C_{\mathrm{reuse}}(x) = \sum_e w_e I_e(x).
$$

Plain-text equivalent:

```text
reuse_cost = sum of the weights of activated penalty pairs

a pair activates when:
  - both buffers are in the same pool
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

### Penalty categories

Categories record the mechanism and evidence. They currently share one additive
`reuse_cost`; they are not separate objective dimensions.

- `pipeline_serialization` is exported by PyPTO. Cross-stage reuse can create a
  false WAR and collapse ping-pong overlap; PyPTO
  [PR #1949](https://github.com/hw-native-sys/pypto/pull/1949) grounds the
  mechanism.
- `load_motion_serialization` exists in the schema and model only. It represents
  reuse that forces a load moved early by `CanonicalizeIOOrder` back behind the
  previous consumer.
- `cross_pipe` exists in the schema and model only. It represents reuse across
  MTE, Vector, or Cube activity that may make PTOAS add set/wait
  synchronization.
- `cross_core` exists in the schema and model only. It represents a potentially
  more expensive cross-core dependency class.
- `event_budget` is an experimental placeholder. Limited event identifiers may
  require a hard resource bound instead of an additive cost.
- `generic` supports tests and external experiments without a stronger
  compiler-derived classification.

If one pair has several penalty records, their activated weights add. For
example, a cost-4 pipeline record and cost-7 cross-pipe record contribute 11
when that pair overlaps. Producers should not emit accidental duplicate records.

Only `pipeline_serialization` is currently generated by the production adapter.
The other reasons exist so controlled experiments can remain typed rather than
mixing unrelated mechanisms into a generic weight.

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

$$
[x_i, x_i+s_i) \cap [x_j, x_j+s_j) = \varnothing.
$$

Plain-text equivalent:

```text
different requested pipeline stages must use disjoint physical ranges
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

$$
C_{\mathrm{pipeline}}(x) =
\sum_{(i,j)\in P} I_{ij}(x).
$$

This is simple and reproducible, but a group with more member pairs can receive
more cost without losing more effective depth. It is a baseline model, not a
calibrated latency prediction.

## Objective ordering

The relaxed problem uses the lexicographic objective:

$$
\operatorname{lex}\left(
O_{\mathrm{capacity}},
C_{\mathrm{reuse}},
H_{\mathrm{total}},
H_{\mathrm{max}}
\right).
$$

Plain-text equivalent:

```text
priority 1: minimize bytes above pool capacities
priority 2: among equally fitting placements, minimize reuse penalties
priority 3: then minimize the sum of per-pool peaks
priority 4: then minimize the largest per-pool peak
```

This intentionally prefers a slightly larger fitting placement with less
expected serialization:

```text
placement A: fits, reuse cost 10, peak 60 KiB
placement B: fits, reuse cost  0, peak 63 KiB
winner: B
```

Capacity remains dominant:

```text
placement A: overflow 0, reuse cost 100
placement B: overflow 1, reuse cost   0
winner: A
```

Raw objective components are always reported. Bytes are not converted to cycles
using an arbitrary scalar weight.

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
- `F_g(x)` be the number of physically independent stage residues retained by
  placement `x`;
- `w_g` be a measured group importance.

A candidate group cost is:

$$
C_{\mathrm{depth}}(x) = \sum_g w_g \left(D_g - F_g(x)\right).
$$

Plain-text equivalent:

```text
penalize lost pipeline depth per group, not every overlapping member pair
```

This avoids pair-count bias, but requires a precise definition of physical
residue equivalence and device calibration of `w_g`.

### Candidate 3: PTOAS-induced synchronization or critical path

General physical reuse can make PTOAS add an anti-dependency, event, wait, or
barrier. A pairwise approximation may classify MTE-to-Vector/Cube reuse and
reuse that cancels deliberate early load motion.

A stronger formulation evaluates the dependency graph after adding
placement-induced edges and minimizes critical-path growth. This is not
equivalent to summing pair costs: an already implied edge can be free, while
several individually cheap edges can form a long serial chain.

This candidate needs PTOAS instrumentation or bounded placement-to-PTOAS
feedback. PTOAS
[PR #948](https://github.com/hw-native-sys/PTOAS/pull/948) addresses downstream
correctness for explicit physical overlap; it does not by itself provide
calibrated performance costs.

Event identifiers should be evaluated separately. Palette exhaustion has a
discontinuous effect and may be better expressed as a hard resource constraint
than as another additive penalty.

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
`pypto_hard_v1` does not impose whole-slot reuse.

The external solver dependency is temporary. Once an algorithm and objective
are selected and validated, the heuristic can be ported into PyPTO and the
dependency removed.

## Device-calibration plan

Controlled experiments must hold the scheduled program, tiling, and codegen
inputs fixed while changing only the placement policy.

Record:

1. capacity feasibility and per-pool peaks;
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
