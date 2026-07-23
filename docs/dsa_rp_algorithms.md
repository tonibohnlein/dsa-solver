# DSA-RP algorithms

The DSA-with-reuse-penalties (DSA-RP) methods solve a fixed-capacity problem:
fit every arena, then minimize the weight of soft pairs whose placed byte
ranges overlap. Each method uses the same normalized colocation classes,
hard-conflict graph, aggregated soft-pair weights, objective evaluator, and
independent validator.

## Controls and constructive methods

| Solver | Policy | Guarantee |
| --- | --- | --- |
| `first_fit` | Lowest-address hard-conflict decode in size, birth, and soft-weight orders, then rescore | Decoder never changes geometry in response to an activated penalty |
| `promote_all` | Exact feasibility test after treating every soft pair as hard | Certified zero-penalty fit or certified no-fit within its search budget |
| `unit_random_coloring` | Seeded independent-uniform color samples | Geometry-free unit-size control |
| `unit_low_rank_rounding` | Nonconvex low-rank vector heuristic plus Gaussian argmax rounding | Geometry-free unit-size research baseline; not an SDP solver and has no Frieze-Jerrum guarantee |
| `canonical_greedy` | Try zero and hard/soft-neighbor tops; choose incremental penalty, then address | Heuristic |
| `promote_repair` | Promote all, then demote the cheapest soft support on an overflowing chain | Heuristic |

Canonical greedy uses the extended support menu:

```text
{0} union {top(i): i is placed and (i,j) is hard or soft}
```

Promote-and-repair reports demotions and falls back to canonical greedy when
the decoded overflow chain contains no soft support. A decoder failure is not
reported as mathematical infeasibility. Forwarding its decoded support chains
into a shared, unverified `CorePool` is not implemented yet; the standalone
heuristic does not claim that integration.

## General exact methods

| Solver | Method | Certificate |
| --- | --- | --- |
| `canonical_branch_and_bound` | Exhaustive canonical hard-or-soft support search | Lexicographic optimum or infeasibility when the node budget is not exhausted |
| `implicit_hitting_set` | Exact weighted hitting-set master plus canonical promoted-DSA oracle and core shrinking | Minimum reuse cost; peak ties are not optimized |

Canonical branch-and-bound seeds its upper bound from first-fit,
canonical-greedy, and promote-and-repair placements, then exhaustively searches
the canonical hard-or-soft support space. Its current bound stack contains the
monotone activated cost and peak; load/color cuts and a shared core-LP bound
are performance extensions, not prerequisites for its certificate.

The implicit-hitting-set solver caches feasible promoted sets and verified
infeasible cores. Its metrics include master nodes, oracle calls, cache hits,
oracle search nodes, and shrink calls. Both engines return `timeout` when a
configured search bound prevents a proof; complete timeout incumbents are
retained.

## Exact portfolio

The portfolio dispatcher tries the following specializations before general
branch-and-bound:

| Code | Solver | Accepted class |
| ---: | --- | --- |
| 1 | `span_one_min_cost_flow` | Uniform unit sizes; each temporal component is a clique; soft pairs join adjacent phases |
| 2 | `capacity_two_exact` | Uniform unit sizes and capacity exactly two units |
| 3 | `treewidth_partition_dp` | Uniform unit sizes; deterministic min-fill width and DP tables within configured limits |
| 4 | `canonical_branch_and_bound` | General fixed-capacity fallback |

The span-one solver reduces each adjacent phase boundary to a
minimum-cardinality, minimum-cost bipartite matching. The capacity-two solver
bipartitions hard components and exactly enumerates component flips. The
treewidth solver performs exact min-sum variable elimination over the combined
hard-and-soft graph. `unsupported` and bounded-search `timeout` results fall
through to later exact methods. Valid timeout incumbents are retained and are
returned only if no later method completes a proof.

## Scale-separated bicriteria baseline

`scale_separated_grid_dp` is intentionally outside the exact portfolio. It
applies when every soft-edge endpoint is large relative to capacity and every
soft edge has bounded temporal span:

1. choose the largest scale threshold supported by the soft endpoints;
2. round large sizes to an alignment-compatible grid;
3. optimize their rounded overlap cost with a sweep dynamic program;
4. stack soft-edge-free small buffers in harmonic size-class tracks.

The returned penalty is no larger than the capacity-C optimum when the input
is feasible at C, but the placement may use unbounded augmented height: the
current harmonic tracks can consume one capacity-sized band per size class.
Such a placement is reported as `best_effort_no_fit`, remains structurally
valid when capacity is removed, and records the DP state count, grid size,
observed span, and small-band height. This is an unconstrained-height research
heuristic, not the paper's BKKRT-based constant-augmentation algorithm. Its
small band accepts singleton, single-interval buffers with only temporal hard
conflicts.

## Promoted-set local search

`reuse_penalty_local_search` searches a pair `(order, S)`, where `S` is the set
of soft pairs temporarily promoted to hard separations. Its neighborhood
contains:

- promotion and demotion of one soft pair;
- targeted order swaps and relocations from activated-edge, peak, and
  two-level hard/soft neighborhoods;
- deterministic full first-fit re-decoding after each move.

Canonical-greedy and promote-and-repair solutions are converted into explicit
`(order, S)` state seeds as well as retained as output incumbents. The search
uses a decaying acceptance probability for selected worsening perturbations.
This reaches placements unavailable to order-only first-fit search. It is a
heuristic and reports evaluations, accepted moves, order moves, soft moves,
and the final promoted-set size. Full re-decoding is the documented pragmatic
first implementation; checkpointed incremental decoding and parallel capacity
frontiers remain future optimizations.

## CLI

```bash
dsa-bench --input instance.json --solver canonical-greedy
dsa-bench --input instance.json --solver reuse-penalty-local-search \
  --seed 7 --iterations 20000 --restarts 4
dsa-bench --input instance.json --solver reuse-penalty-portfolio \
  --iterations 2000000
dsa-bench --input instance.json --solver scale-separated-grid-dp \
  --scale-max-denominator 8 --scale-epsilon-denominator 2 \
  --scale-max-span 4
```

All methods currently target the additive pair-cost model.
Synchronization-covering OR-groups require an additional model/output
contract and are not mislabeled as implemented pairwise solvers.
