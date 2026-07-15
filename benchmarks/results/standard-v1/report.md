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

- MiniMalloc certified all 104/104 PyPTO and 219/219 PyPTO-Lib reference peaks, but only 1/11 public MiniMalloc cases under the bounded timeout.
- TVM hill climb and local search both match the reference on all 104/104 PyPTO and 219/219 PyPTO-Lib instances (local-search wins: 104/104 and 219/219).
- The public MiniMalloc family remains discriminating: TVM hill climb has a geometric-mean peak ratio of 1.08168, versus 1.10355 for local search.

## Solution quality

Each solver cell is `geometric-mean peak / reference peak (wins/N)`. The reference is the lowest independently validated peak found for that instance. The highlights state where MiniMalloc certified it as optimal. Lower is better, `1.000` is ideal, and a win includes ties.

| Instance corpus | N | MiniMalloc exact | First fit | XLA heap | TVM hill climb | Local search |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| MiniMalloc benchmark corpus | 11 | 1.02636 (9/11) | 1.21673 (0/11) | 1.22902 (0/11) | 1.08168 (1/11) | 1.10355 (1/11) |
| PyPTO | 104 | 1.00000 (104/104) | 1.00000 (104/104) | 1.00000 (104/104) | 1.00000 (104/104) | 1.00000 (104/104) |
| PyPTO-Lib | 219 | 1.00000 (219/219) | 1.00004 (213/219) | 1.00052 (212/219) | 1.00000 (219/219) | 1.00000 (219/219) |

## Runtime relative to first fit

Each cell is the geometric mean of the per-instance `solver median / first-fit median` ratio. Normalizing before aggregation controls for instance size and difficulty; `1.00x` is first-fit speed. First fit and XLA use the median of 5 repetitions per instance; stochastic searches use the median of 3 seeds; MiniMalloc is one bounded run. Runtime is machine-dependent.

| Instance corpus | N | MiniMalloc exact | First fit | XLA heap | TVM hill climb | Local search |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| MiniMalloc benchmark corpus | 11 | 3035.43x | 1.00x | 1.00x | 3196.51x | 2118.65x |
| PyPTO | 104 | 4.99x | 1.00x | 1.02x | 354.09x | 1763.09x |
| PyPTO-Lib | 219 | 6.30x | 1.00x | 1.00x | 863.08x | 1872.61x |
