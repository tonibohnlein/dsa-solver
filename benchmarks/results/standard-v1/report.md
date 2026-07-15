# Standard DSA benchmark results

This presentation aggregates capacity-free, single-pool standard DSA problems by source family. Public MiniMalloc instances are used directly. PyPTO rows are per-pool projections that retain buffer sizes and lifetimes but remove compiler-specific constraints, alignment, capacity, and architecture resources; they are not device-valid PyPTO placements.

Full per-instance results remain in `summary.csv`; raw repetitions are in `results.jsonl`. The tables below are the presentation layer.

Configuration: run label `standard-v1`; seeds `0,1,2`; search budget `2000`; local-search restarts `4`; deterministic repetitions `5`; MiniMalloc timeout `5000 ms` per instance. Peak is the best valid result. Runtime is the median across deterministic repetitions or stochastic seeds; MiniMalloc is one bounded run. All returned placements are independently validated.

Regenerate from the repository root:

```bash
./build/dsa-suite \
  --standard 'third_party/minimalloc/benchmarks/challenging' \
  --pypto 'benchmarks/pypto' \
  --pypto 'benchmarks/pypto-lib' \
  --output-dir 'benchmarks/results/standard-v1' \
  --run-label 'standard-v1' \
  --seeds '0,1,2' \
  --iterations '2000' \
  --restarts '4' \
  --stagnation '250' \
  --deterministic-repetitions '5' \
  --minimalloc-timeout-ms '5000' \
  --standard-only
```

## Highlights

- MiniMalloc certified 324/334 reference peaks: all 323 PyPTO projections, but only 1/11 public MiniMalloc cases under the bounded timeout.
- On PyPTO projections, TVM hill climb and local search match the reference on 323/323 and 323/323 instances; first fit reaches 317/323 and XLA reaches 316/323.
- The public MiniMalloc family remains discriminating: TVM hill climb has a geometric-mean peak ratio of 1.08168, versus 1.10355 for local search.

## Solution quality

Each solver cell is `geometric-mean peak / reference peak (wins/N)`. The reference is the lowest independently validated peak found for that instance; the `Proven` column reports how often MiniMalloc certified that reference as optimal. Lower is better, `1.000` is ideal, and a win includes ties.

| Instance family | N | Proven | MiniMalloc exact | First fit | XLA heap | TVM hill climb | Local search |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Overall | 334 | 324/334 | 1.00086 (332/334) | 1.00651 (317/334) | 1.00716 (316/334) | 1.00259 (324/334) | 1.00325 (324/334) |
| All PyPTO projections | 323 | 323/323 | 1.00000 (323/323) | 1.00003 (317/323) | 1.00035 (316/323) | 1.00000 (323/323) | 1.00000 (323/323) |
| MiniMalloc | 11 | 1/11 | 1.02636 (9/11) | 1.21673 (0/11) | 1.22902 (0/11) | 1.08168 (1/11) | 1.10355 (1/11) |
| PyPTO / control flow | 4 | 4/4 | 1.00000 (4/4) | 1.00000 (4/4) | 1.00000 (4/4) | 1.00000 (4/4) | 1.00000 (4/4) |
| PyPTO / cross-core | 10 | 10/10 | 1.00000 (10/10) | 1.00000 (10/10) | 1.00000 (10/10) | 1.00000 (10/10) | 1.00000 (10/10) |
| PyPTO / examples | 11 | 11/11 | 1.00000 (11/11) | 1.00000 (11/11) | 1.00000 (11/11) | 1.00000 (11/11) | 1.00000 (11/11) |
| PyPTO / framework and models | 21 | 21/21 | 1.00000 (21/21) | 1.00000 (21/21) | 1.00000 (21/21) | 1.00000 (21/21) | 1.00000 (21/21) |
| PyPTO / operations | 56 | 56/56 | 1.00000 (56/56) | 1.00000 (56/56) | 1.00000 (56/56) | 1.00000 (56/56) | 1.00000 (56/56) |
| PyPTO / regression fixtures | 2 | 2/2 | 1.00000 (2/2) | 1.00000 (2/2) | 1.00000 (2/2) | 1.00000 (2/2) | 1.00000 (2/2) |
| PyPTO-Lib / DeepSeek v3.2 | 27 | 27/27 | 1.00000 (27/27) | 1.00000 (27/27) | 1.00000 (27/27) | 1.00000 (27/27) | 1.00000 (27/27) |
| PyPTO-Lib / DeepSeek v4 | 101 | 101/101 | 1.00000 (101/101) | 1.00005 (99/101) | 1.00109 (97/101) | 1.00000 (101/101) | 1.00000 (101/101) |
| PyPTO-Lib / Qwen3 14B | 70 | 70/70 | 1.00000 (70/70) | 1.00003 (68/70) | 1.00002 (69/70) | 1.00000 (70/70) | 1.00000 (70/70) |
| PyPTO-Lib / Qwen3 32B | 14 | 14/14 | 1.00000 (14/14) | 1.00020 (12/14) | 1.00020 (12/14) | 1.00000 (14/14) | 1.00000 (14/14) |
| PyPTO-Lib / examples | 7 | 7/7 | 1.00000 (7/7) | 1.00000 (7/7) | 1.00000 (7/7) | 1.00000 (7/7) | 1.00000 (7/7) |

## Runtime

Each cell is the geometric mean of per-instance runtime. First fit and XLA use the median of 5 repetitions per instance; stochastic searches use the median of 3 seeds; MiniMalloc is one bounded run. Runtime is machine-dependent.

| Instance family | N | MiniMalloc exact | First fit | XLA heap | TVM hill climb | Local search |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Overall | 334 | 78 us | 11 us | 11 us | 7.45 ms | 20.12 ms |
| All PyPTO projections | 323 | 54 us | 9 us | 9 us | 6.03 ms | 17.09 ms |
| MiniMalloc | 11 | 3.45 s | 1.13 ms | 1.13 ms | 3.63 s | 2.40 s |
| PyPTO / control flow | 4 | 59 us | 7 us | 8 us | 6.42 ms | 11.40 ms |
| PyPTO / cross-core | 10 | 27 us | 5 us | 5 us | 3.67 ms | 8.92 ms |
| PyPTO / examples | 11 | 46 us | 8 us | 8 us | 2.70 ms | 13.39 ms |
| PyPTO / framework and models | 21 | 41 us | 9 us | 9 us | 3.62 ms | 15.16 ms |
| PyPTO / operations | 56 | 17 us | 4 us | 4 us | 1.06 ms | 6.64 ms |
| PyPTO / regression fixtures | 2 | 11 us | 2 us | 2 us | 226 us | 3.14 ms |
| PyPTO-Lib / DeepSeek v3.2 | 27 | 26 us | 3 us | 4 us | 2.27 ms | 8.32 ms |
| PyPTO-Lib / DeepSeek v4 | 101 | 89 us | 15 us | 15 us | 12.76 ms | 26.59 ms |
| PyPTO-Lib / Qwen3 14B | 70 | 109 us | 17 us | 17 us | 17.38 ms | 30.55 ms |
| PyPTO-Lib / Qwen3 32B | 14 | 98 us | 18 us | 19 us | 13.38 ms | 32.95 ms |
| PyPTO-Lib / examples | 7 | 28 us | 5 us | 5 us | 2.47 ms | 8.00 ms |
