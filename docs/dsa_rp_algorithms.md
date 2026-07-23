# DSA-RP algorithms

The DSA-with-reuse-penalties (DSA-RP) methods solve a fixed-capacity problem:
fit every arena, then minimize the weight of soft pairs whose placed byte
ranges overlap. Each method uses the same normalized colocation classes,
hard-conflict graph, aggregated soft-pair weights, objective evaluator, and
independent validator.

## Controls and constructive methods

| Solver | Policy | Guarantee |
| --- | --- | --- |
| `first_fit` | Lowest-address hard-conflict decode, then rescore | Penalty-blind control |
| `promote_all` | Treat every soft pair as hard | Zero penalty when it fits; no repair |
| `unit_random_coloring` | One seeded independent-uniform coloring | Geometry-free unit-size control |
| `canonical_greedy` | Try zero and hard/soft-neighbor tops; choose incremental penalty, then address | Heuristic |
| `promote_repair` | Promote all, then demote the cheapest soft support on an overflowing chain | Heuristic |

Canonical greedy uses the extended support menu:

```text
{0} union {top(i): i is placed and (i,j) is hard or soft}
```

Promote-and-repair reports demotions and falls back to canonical greedy when
the decoded overflow chain contains no soft support. A decoder failure is not
reported as mathematical infeasibility.

## General exact methods

| Solver | Method | Certificate |
| --- | --- | --- |
| `canonical_branch_and_bound` | Exhaustive canonical hard-or-soft support search | Lexicographic optimum or infeasibility when the node budget is not exhausted |
| `implicit_hitting_set` | Exact weighted hitting-set master plus canonical promoted-DSA oracle and core shrinking | Minimum reuse cost; peak ties are not optimized |

The implicit-hitting-set solver caches feasible promoted sets and verified
infeasible cores. Its metrics include master nodes, oracle calls, cache hits,
oracle search nodes, and shrink calls. Both engines return `timeout` when a
configured search bound prevents a proof.

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
hard-and-soft graph. Each specialization either proves its stated objective or
returns `unsupported`; it never silently falls back under its own name.

## Promoted-set local search

`reuse_penalty_local_search` searches a pair `(order, S)`, where `S` is the set
of soft pairs temporarily promoted to hard separations. Its neighborhood
contains:

- promotion and demotion of one soft pair;
- order swaps and relocations;
- deterministic full first-fit re-decoding after each move.

This reaches placements unavailable to order-only first-fit search. It is a
heuristic and reports evaluations, accepted moves, order moves, soft moves,
and the final promoted-set size.

## CLI

```bash
dsa-bench --input instance.json --solver canonical-greedy
dsa-bench --input instance.json --solver reuse-penalty-local-search \
  --seed 7 --iterations 20000 --restarts 4
dsa-bench --input instance.json --solver reuse-penalty-portfolio \
  --iterations 2000000
```

All exact specializations currently target the additive pair-cost model. The
paper's scale-separated small-buffer band and synchronization-covering
OR-groups require additional model/output contracts and are not mislabeled as
implemented solvers.
