# Results

`standard-v1/` is the checked-in capacity-free standard-DSA comparison. It
contains 11 MiniMalloc benchmark instances and 323 unique, nontrivial per-pool
projections from PyPTO and PyPTO-Lib. Projection removes compiler constraints,
alignment, capacity, and architecture resources; these rows are not valid
placements for `pypto_hard_v1`.

`dsa-rp-v1/` compares the DSA-RP algorithms on 204 matched problems. In one
variant, mechanically recognized cross-pipe reuse relations are hard
separations; in the other, the same relations are unit-weight soft penalties.

Each snapshot retains:

- `report.md`: the readable aggregate comparison;
- `summary.csv`: per-instance best peak and median runtime;
- `results.jsonl`: all repetitions, seeds, statuses, and validation results.

The DSA-RP snapshot also has `algorithm-comparison.csv` and
`paired-objectives.csv`. Its report explains the penalty-specific columns and
records the exact reproduction command.

For `standard-v1`, quality is the geometric mean of
`solver peak / best validated peak`, with win counts. Runtime is normalized
per instance against first fit before aggregation; raw medians remain in
`summary.csv`.

A retained projection has both an overlapping buffer pair and a non-overlapping
pair. The current filter removed 474 no-choice pools and 120 structural
duplicates.

The exact reproduction command is recorded in `standard-v1/report.md`. Append
`--report-only` to rebuild only the Markdown presentation from the existing
JSONL; this does not execute solvers or modify raw results.
