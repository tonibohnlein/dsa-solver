# Results

`standard-v1/` is the checked-in capacity-free standard-DSA comparison. It
contains 11 MiniMalloc benchmark instances and 323 unique, nontrivial per-pool
projections from PyPTO and PyPTO-Lib. Projection removes compiler constraints,
alignment, capacity, and architecture resources; these rows are not valid
placements for `pypto_hard_v1`.

The snapshot contains:

- `report.md`: three-corpus quality and relative-runtime summary;
- `summary.csv`: per-instance best peak and median runtime;
- `results.jsonl`: all repetitions, seeds, statuses, and validation results.

Quality is the geometric mean of `solver peak / best validated peak`, with win
counts. The report separately states where MiniMalloc certified the reference
as optimal. Runtime is normalized per instance against first fit before ratios
are geometrically averaged; raw medians remain in `summary.csv`.

A retained projection has both an overlapping buffer pair and a non-overlapping
pair. The current filter removed 474 no-choice pools and 120 structural
duplicates.

The exact reproduction command is recorded in `standard-v1/report.md`. Append
`--report-only` to rebuild only the Markdown presentation from the existing
JSONL; this does not execute solvers or modify raw results.
