# DSA-RP constructive baselines

These deterministic baselines implement the constructive methods in the
DSA-with-reuse-penalties algorithms section. They solve the
capacity-constrained problem: placements must fit every fixed arena capacity,
then the reported objective measures activated reuse penalties.

| Method | Policy | Purpose |
| --- | --- | --- |
| `first_fit` | Decode hard conflicts at the lowest address, then rescore | Penalty-blind control |
| `canonical_greedy` | Consider address zero and the tops of placed hard or soft neighbors; choose incremental penalty, then address | Cheapest extended-support greedy probe |
| `promote_repair` | Initially make every soft edge hard; on overflow, demote the cheapest soft edge on the decoded support chain | Strict-separation baseline with capacity repair |

All three return independently validated placements. Canonical greedy and
promote-and-repair require fixed capacities. Their capability checks reject
unsupported flexible pools, pins, and bank constraints rather than dropping
them.

## Canonical greedy

For the next buffer `j`, the candidate set is:

```text
{0} union {top(i): i is placed and (i,j) is a hard or soft edge}
```

Reserved-range ends are also candidates. After alignment and hard-feasibility
filtering, the solver minimizes:

```text
(newly activated reuse weight, address)
```

The menu contains the canonical hard-or-soft supports from the paper, but the
greedy choice is not exact. The implementation uses the solver's deterministic
default order and reports the number of candidate offsets evaluated.

## Promote-and-repair

The solver decodes with all soft edges promoted to separations. If that
placement exceeds capacity, it starts at an overflowing buffer, follows exact
support pointers toward address zero, and demotes the cheapest active soft edge
on that chain before decoding again.

This is a heuristic repair, not an infeasibility proof or an optimal penalty
solver. A hard-only support chain triggers a documented fallback to canonical
greedy. Metrics report the initial and remaining active soft edges, demotions,
packing attempts, and fallback use.

## Use

```bash
dsa-bench --input instance.json --solver canonical-greedy
dsa-bench --input instance.json --solver promote-repair
```

`dsa-suite` records both methods for structured, fixed-capacity instances. The
exact implicit-hitting-set, canonical branch-and-bound, portfolio, and
promoted-set local-search methods remain separate future engines; this
implementation does not use those names for approximate substitutes.
