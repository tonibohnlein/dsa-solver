# DSA with reuse penalties

The corpus contains 204 matched hard/soft problems. The soft problem fits within the architecture capacity and minimizes activated unit-weight cross-pipe reuse edges. The hard counterpart forbids all of the same overlaps. All reported placements were independently validated by `dsa-suite`. Pre-existing non-cross-pipe penalties are unchanged in both variants, so a fitting hard placement contributes its remaining penalty as a valid upper bound for the soft problem.

Full per-run objectives and diagnostics are in `results.jsonl`; per-algorithm best objectives are in `summary.csv`; `paired-objectives.csv` records the compact per-instance comparison.

Configuration: seeds `0,1,2`, search budget `2000`, restarts `4`, and stagnation limit `250`. Runtime is machine-dependent.

Regenerate from the repository root:

```bash
./build/dsa-suite \
  --pypto benchmarks/pypto \
  --pypto benchmarks/pypto-lib \
  --dsa-rp-variants-only \
  --output-dir benchmarks/results/dsa-rp-v1 \
  --run-label dsa-rp-v1 \
  --seeds 0,1,2 \
  --iterations 2000 \
  --restarts 4 \
  --stagnation 250 \
  --no-minimalloc \
  --no-core-relaxations
python tools/report_dsa_rp_results.py benchmarks/results/dsa-rp-v1/results.jsonl \
  --output-dir benchmarks/results/dsa-rp-v1
```

## Corpus outcome

| Source | Pairs | Edges | Strict fit found | Soft solver found 0 | Best-known cost 0 | Optimum proven | Sum best cost |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| PyPTO | 54 | 1170 | 40 | 40 | 40 | 51 | 90 |
| PyPTO-Lib | 150 | 4254 | 126 | 115 | 115 | 136 | 439 |

No tested method found a capacity-fitting placement for 1 pair(s): `mm_512x512x192_acc_coalesce`. They remain in every denominator.

## Soft-penalty algorithm comparison

`Fits` counts capacity-feasible validated placements. `Best` counts ties with the lowest validated reuse cost found across both counterparts. `Proven` counts solver certificates, not merely best-known values. `Mean active` averages `reuse_cost / recognized_edges` per fitted instance. Runtime is a geometric mean of per-instance median ratios to first fit; its parenthesized value is the number of positive-resolution comparisons.

| Class | Algorithm | Applicable | Fits | Best | Zero | Proven | Sum cost | Mean active | Runtime / FF |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Baseline | `first_fit` | 204/204 | 203/204 | 26/204 | 23/204 | 0/204 | 2355 | 62.64% | 1.00x (203) |
| Baseline | `cypress_relaxation` | 204/204 | 203/204 | 165/204 | 142/204 | 0/204 | 712 | 14.35% | 0.60x (203) |
| Constructive | `canonical_greedy` | 204/204 | 203/204 | 203/204 | 155/204 | 0/204 | 529 | 9.17% | 1.50x (203) |
| Constructive | `promote_repair` | 204/204 | 203/204 | 194/204 | 155/204 | 0/204 | 580 | 9.63% | 1.00x (203) |
| Constructive | `promote_all` | 204/204 | 155/204 | 155/204 | 155/204 | 155/204 | 0 | 0.00% | 1.01x (155) |
| Local search | `reuse_penalty_local_search` | 204/204 | 203/204 | 203/204 | 155/204 | 0/204 | 529 | 9.17% | 467.74x (203) |
| Exact | `canonical_branch_and_bound` | 204/204 | 203/204 | 203/204 | 155/204 | 186/204 | 529 | 9.17% | 4.46x (203) |
| Exact | `implicit_hitting_set` | 204/204 | 203/204 | 203/204 | 155/204 | 186/204 | 529 | 9.17% | 7.58x (203) |
| Exact | `capacity_two_exact` | 2/204 | 2/204 | 2/204 | 2/204 | 2/204 | 0 | 0.00% | 8.01x (2) |
| Exact | `span_one_min_cost_flow` | 15/204 | 15/204 | 15/204 | 12/204 | 15/204 | 5 | 15.00% | 0.63x (15) |
| Exact | `treewidth_partition_dp` | 23/204 | 23/204 | 23/204 | 19/204 | 23/204 | 9 | 11.23% | 1.15x (23) |
| Exact | `reuse_penalty_portfolio` | 204/204 | 203/204 | 203/204 | 155/204 | 186/204 | 529 | 9.17% | 3.81x (203) |
| Bicriteria | `scale_separated_grid_dp` | 13/204 | 11/204 | 11/204 | 10/204 | 0/204 | 1 | 9.09% | 1.53x (11) |
| Unit control | `unit_random_coloring` | 6/204 | 6/204 | 6/204 | 6/204 | 0/204 | 0 | 0.00% | 0.39x (6) |
| Unit control | `unit_low_rank_rounding` | 6/204 | 6/204 | 6/204 | 6/204 | 0/204 | 0 | 0.00% | 6.31x (6) |
| Legacy search | `tvm_hill_climb` | 204/204 | 203/204 | 27/204 | 24/204 | 0/204 | 2186 | 62.09% | 74.82x (203) |
| Legacy search | `local_search` | 204/204 | 203/204 | 54/204 | 51/204 | 0/204 | 1885 | 49.40% | 324.01x (203) |
| Legacy search | `pypto_structured_search` | 204/204 | 203/204 | 56/204 | 53/204 | 0/204 | 1873 | 49.39% | 323.75x (203) |

**Cypress comparison.** On this corpus, Cypress relaxation produces substantially worse objectives than canonical greedy: its aggregate penalty is 712 versus 529 (34.6% higher), it reaches the best-known objective on 165 versus 203 instances, and it finds 142 versus 155 zero-penalty placements. The runtime ratios are 0.60x and 1.50x first fit, respectively.

## Hard-counterpart search

These rows ask only whether the same relations can all remain hard while fitting capacity. Failure is a bounded-search result unless an algorithm supplies an infeasibility certificate.

| Algorithm | Applicable | Fits | Runtime / FF |
| --- | ---: | ---: | ---: |
| `first_fit` | 204/204 | 166/204 | 1.00x (166) |
| `canonical_greedy` | 204/204 | 166/204 | 2.25x (166) |
| `promote_repair` | 204/204 | 166/204 | 1.68x (166) |
| `cypress_relaxation` | 204/204 | 166/204 | 1.68x (166) |
| `tvm_hill_climb` | 204/204 | 166/204 | 432.62x (166) |
| `local_search` | 204/204 | 166/204 | 867.96x (166) |
| `pypto_structured_search` | 204/204 | 166/204 | 869.10x (166) |
