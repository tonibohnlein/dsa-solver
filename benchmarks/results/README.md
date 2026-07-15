# Reproducible benchmark results

`standard-v1/` is the only checked-in result snapshot. It compares every
applicable algorithm on capacity-free standard DSA:

- the public MiniMalloc A--K instances are used directly;
- PyPTO and PyPTO-Lib inputs are projected into independent single-pool
  standard problems;
- compiler constraints, alignment, capacity, and architecture resources are
  removed from those projections;
- projections with no placement choice and structurally duplicate projections
  are omitted.

A retained projection has at least one overlapping buffer pair and at least one
non-overlapping pair. This removes cases whose peak is forced to either the sum
or the largest buffer without excluding harder cases merely because the current
algorithms happen to tie on them.

The current snapshot has 334 rows: 11 public MiniMalloc instances and 323
unique, nontrivial PyPTO/PyPTO-Lib pool projections. Projection retained 323
rows after rejecting 474 no-choice pools and 120 duplicates.

The snapshot contains:

- `report.md`: separate peak-memory and runtime tables;
- `summary.csv`: long-form best-peak and median-runtime aggregation;
- `results.jsonl`: authoritative per-run results, status, validation, seed, and
  configuration metadata.

The PyPTO-derived rows are standard-DSA algorithm benchmarks, not device-valid
PyPTO placements or claims about `pypto_hard_v1`. Structured PyPTO comparisons
will be added only after that problem variant is finalized.

Runtime is machine-dependent. Regenerate the complete snapshot using the exact
command recorded in `standard-v1/report.md`; review raw and aggregated changes
together. A timed-out MiniMalloc run may retain a feasible peak, but the report
marks it as uncertified rather than calling it optimal.
