# PyPTO hard-v1 problem contract

`pypto_hard_v1` is the smallest schema-v1 profile currently justified for
device-correct PyPTO placement. It separates required constraints from research
hypotheses. `pypto_research_v1` extends the same base contract with explicitly
experimental fields; legacy `pypto_structured` documents remain readable but
make no hard-vs-research claim.

This contract does not claim that issue
[#1908](https://github.com/hw-native-sys/pypto/issues/1908) was an attempt to
introduce XLA logical buffers. The issue describes a concrete fragmentation
failure caused by grouping before offset assignment. XLA's value/buffer/slice
separation is a useful analogue, not the documented origin of the issue.

## Mathematical formulation

Let `B` be physical allocation identities and `P` fixed local-memory pools. For
each buffer `i in B`, the exporter provides:

- size `s_i > 0` bytes and alignment `a_i >= 1`;
- one fixed pool `p_i in P`;
- one conservative half-open lifetime `L_i = [l_i, u_i)`, `l_i < u_i`.

Each pool has an optional capacity `C_p` and a set of half-open reserved address
ranges `R_p`. `S` is the set of hard separation pairs. The decision variable is
the byte offset `x_i` of each buffer. Define its address range as
`A_i = [x_i, x_i + s_i)` and temporal conflict as:

```text
T(i,j) := p_i = p_j and l_i < u_j and l_j < u_i.
```

A hard-v1 placement is feasible exactly when all of the following hold:

```text
x_i >= 0
x_i mod a_i = 0
x_i + s_i <= C_p_i                         when C_p_i exists
A_i intersect r = empty                    for every r in R_p_i
T(i,j) => A_i intersect A_j = empty        for every i != j
(i,j) in S => A_i intersect A_j = empty
p_i = p_j => (x_i = x_j or A_i intersect A_j = empty)
```

The final condition is PyPTO's current `whole_slot_v1` address-reuse contract.
Two lifetime-disjoint buffers may share an equal base, with the slot extent
equal to the larger size, or remain completely disjoint. Partial overlap at
different bases is forbidden because the current downstream dependency path
does not safely represent arbitrary sub-slot tenancy. Consequently hard-v1
does not yet implement the full freed-region subdivision requested by #1908.

Mandatory semantic aliases are materialized before export and become one
buffer identity. Alias-class entries name the collapsed IR values; they are
provenance, not an additional colocation constraint.

For pool high-water marks:

```text
H_p = max({r.end | r in R_p} union {x_i + s_i | p_i = p})
```

The schema-v1 objective is lexicographic minimization of
`(sum_p H_p, max_p H_p)`, subject to the hard capacity constraints above.
Capacity feasibility is the compiler requirement; peak minimization makes
placements deterministic and provides headroom. No synchronization or bank
cost belongs to hard-v1.

## Lifetime event ordering

PyPTO statement points are expanded into read and write sub-points. An
allocation defined at statement `d` starts at `2*d+1`; a final read at statement
`u` ends at `2*u+1`. The interval is therefore:

```text
[2*d + 1, max(2*d + 2, 2*u + 1))
```

This preserves read-before-write reuse at a statement boundary. The interval
is one allocation hull, not the union of SSA-member ranges. The latter produced
a false hole in DeepSeek-v4 softmax-pool loop-carried accumulators; legal solver
reuse inside that hole caused deterministic device corruption.

## Exported feature audit

| Field | Hard-v1 role | Source and motivation | Evidence/status |
| --- | --- | --- | --- |
| buffer identity | hard domain | one physical `MemRef::base_` after mandatory alias materialization | views, loop carries, in-place results, and if-phi writebacks require one allocation identity |
| `size` | hard | allocation extent from lifetime analysis | required for address safety |
| `alignment` | hard | backend allocator policy | required by target memory instructions |
| one lifetime hull | hard | allocation `def_point..last_use_point`, with read/write sub-points | DeepSeek-v4 device A/B proved SSA-member holes unsafe |
| one `allowed_pool` | hard | IR `MemorySpace` already chosen before planning | codegen operations are memory-space specific |
| pool `capacity` | hard when known | backend safe memory limit | over-capacity placement is invalid; Vec uses the target's safe rather than physical limit |
| `reserved_ranges` | hard | resolved `system.reserve_buffer` windows | planner tenants must not overwrite runtime/compiler reservations |
| `whole_slot_reuse` | hard | downstream address-dependency contract | gather regression proved arbitrary partial overlap device-unsafe |
| `separations` | hard | allocation plan | pipeline-stage, target-hazard, and semantic-no-alias rules must survive joint placement |
| alias classes | provenance | names already collapsed into each buffer | audits semantic structure and enables ablatable search moves; no extra feasibility rule |
| pipeline groups | provenance | pipeline membership, stage, residue, requested/effective depth | audits separation coverage and depth shedding; no independent feasibility rule |
| target and producer metadata | provenance | compiler configuration and capture revision | reproducibility only |
| peak objective | required objective | capacity/headroom placement policy | does not model runtime cost |
| adjacent reuse penalties | research only | chronological same-residue pipeline proxy | uncalibrated unit cost; legal only in `pypto_research_v1` |

## Fields outside hard-v1

The general `DsaProblem` retains research mechanisms so they can be evaluated
without changing the portable standard formulation. Hard-v1 rejects them:

| Feature | Why it is not required today | Research gate |
| --- | --- | --- |
| multi-interval lifetime | no physical-liveness proof; prior SSA approximation was unsound | prove live-through/control-flow correctness and rerun model device regression |
| colocations | mandatory aliases are already one exported identity | define a distinct same-offset identity that cannot be coalesced before export |
| temporal exclusions | branch exclusivity is not exported as a physical-liveness proof | independent control-flow proof and validator tests |
| pinned allocations | current adapter expresses occupied windows as pool reservations | demonstrate a live compiler case that requires a fixed named placement |
| flexible pool assignment | PyPTO fixes memory space before this pass | joint schedule/memory-space design and codegen support |
| bank geometry/cost | no calibrated target model | controlled offset-residue experiment with measured stalls/latency |
| reuse/synchronization cost | current unit proxy is not device calibrated | controlled reuse A/B protocol in `device_cost_experiments.md` |
| piecewise size/resizing | schema v1 uses one fixed allocation extent | explicit value/slice representation and downstream dependency support |

`pypto_research_v1` requires the same lifetime-ordering, pre-memory-reuse input,
and whole-slot metadata contracts, but permits these experimental fields. Every
benchmark table must keep hard and research profiles visibly distinct.

## Search gate

`pypto_structured_search` is an experimental DSA heuristic over the common
placement engine. Its structured moves use alias/pipeline provenance, but the
engine and independent validator remain authoritative for hard constraints.
No new move or objective should be called PyPTO-specific progress until:

1. a fixed-exporter corpus reports actual feature occurrence;
2. the move targets a feature present in that corpus;
3. correctness remains valid under hard-v1; and
4. any cost-based improvement predicts and then reproduces a device metric.
