# Reproducible benchmark results

Each newly generated result directory is produced by `dsa-suite` and contains four views of one run:

- `results.jsonl` is the authoritative per-run record;
- `summary.csv` aggregates repeated seeds without discarding status or validity;
- `features.csv` records per-instance constraint and provenance occurrence;
- `report.md` renders separate standard and PyPTO-structured tables.

Older snapshots created before `features.csv` was introduced retain their original three files.

In raw records, `placement_valid` checks address geometry while relaxing capacity only for explicit
best-effort diagnostics. `solution_valid` checks the complete original problem, including capacity.

The checked-in `baseline` snapshot uses bounded budgets and is a regression/reference run, not a claim
that its machine-dependent runtimes are universally representative. Regenerate it from the repository
root with the command recorded at the top of its report. Review changes to raw records and the generated
tables together.

`complete-v1` is the current combined comparison. It covers the MiniMalloc A--K
challenge corpus, the standard freed-region regression, every checked-in sound
PyPTO document, and a generated standard core relaxation for each PyPTO pool.
The trivial MiniMalloc example remains a parser/CLI fixture and is deliberately
excluded from the research table.

`minimalloc-1mib-xla` is the longer standard-DSA comparison. It uses the
MiniMalloc A--K one-MiB capacity, 2,000 ordering-search candidates per seed,
and a 60-second exact-solver budget per instance. This is the long-form
quality/runtime table; `baseline` remains the quicker CI-style snapshot.

New PyPTO or `pypto-lib` corpus entries should record public source revisions, target, memory pool, schema
version, and a content hash in structured-problem metadata. Do not publish instances derived from private
workloads or containing machine-specific paths.
