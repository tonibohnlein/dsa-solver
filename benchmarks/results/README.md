# Reproducible benchmark results

Each result directory is produced by `dsa-suite` and contains three views of one run:

- `results.jsonl` is the authoritative per-run record;
- `summary.csv` aggregates repeated seeds without discarding status or validity;
- `report.md` renders separate standard and PyPTO-structured tables.

In raw records, `placement_valid` checks address geometry while relaxing capacity only for explicit
best-effort diagnostics. `solution_valid` checks the complete original problem, including capacity.

The checked-in `baseline` snapshot uses bounded budgets and is a regression/reference run, not a claim
that its machine-dependent runtimes are universally representative. Regenerate it from the repository
root with the command recorded at the top of its report. Review changes to raw records and the generated
tables together.

`minimalloc-1mib-xla` is the longer standard-DSA comparison. It uses the
MiniMalloc A--K one-MiB capacity, 2,000 ordering-search candidates per seed,
and a 60-second exact-solver budget per instance. This is the long-form
quality/runtime table; `baseline` remains the quicker CI-style snapshot.

New PyPTO or `pypto-lib` corpus entries should record public source revisions, target, memory pool, schema
version, and a content hash in structured-problem metadata. Do not publish instances derived from private
workloads or containing machine-specific paths.
