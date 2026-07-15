# Full PyPTO/PyPTO-Lib corpus capture

## 0. Goal and stop conditions

Produce the first complete benchmark corpus from the sound allocation-lifetime
exporter, then run every applicable solver on both standard projections and
full PyPTO problems.

Stop before compilation if any pinned revision is unfetchable. Stop a case on
device reset, unbounded hang, skipped golden validation, missing DSA export, or
schema/profile failure. Never substitute an older exporter or import the known
unsound 597-document archive.

## 1. Exact revisions

Record and verify these revisions before building:

| Component | Revision |
| --- | --- |
| PyPTO | `8df2ed4bc56d73a9db434f42a6c6fe937dcb08d1` |
| PyPTO-Lib | `6e897cd99c28767b22e05f209da3e041f15c3dfc` |
| dsa-solver | `574c1c2443d6ece833caba17908e9b1d16cf1774` |

Also record runtime, pto-isa, PTOAS release and archive hash, CANN, Python,
architecture, and device IDs. Keep every worktree clean. Build with at most two
jobs and use one codegen worker per process.

## 2. Host gates

1. Build dsa-solver with testing and MiniMalloc enabled; require all CTest tests
   to pass and install it into a fresh prefix.
2. Build PyPTO with `PYPTO_ENABLE_DSA_SOLVER=ON` against that prefix.
3. Prove `is_dsa_solver_available()`, `MemoryPlanner.DSA`, and export-directory
   round-trip.
4. Run focused allocator/exporter tests and PyPTO-Lib golden unit tests.
5. Print imported `pypto`, `pypto_core`, and `_task_interface` paths; do not copy
   extensions between checkouts.

## 3. PyPTO correctness and system-test capture

First run the four cases in
`benchmarks/capture/pypto-adapter-gates-8df2ed4.tsv` under DSA and require
golden success. Preserve their exports under top-level case IDs `colvec`,
`gather`, `pipeline`, and `targeted`.

Then run every `tests/st` file under `--memory-planner=dsa` with a per-file
timeout. Use a dedicated export root per file so parameterized observations can
be traced to their source without becoming separate coverage claims. Run a
PyPTO baseline only to classify DSA failures. Preserve all raw exports, but let
structural deduplication decide benchmark weight.

Known environment-only distributed/HCCL failures must be reported, not silently
removed. A failed test may still yield diagnostic exports, but those documents
do not enter the accepted corpus until correctness is resolved.

## 4. PyPTO-Lib exhaustive capture

Use `benchmarks/capture/pypto-lib-6e897cd.tsv` as the source inventory:

- 61 discovered entry points total;
- 58 `capture` targets that must each emit at least one DSA document;
- three reviewed exclusions with zero expected documents.

The execution wrapper must reproduce direct script execution by prepending the
script directory to `sys.path`, propagate `MemoryPlanner.DSA` and a case-local
`dsa_export_dir` through `RunConfig`, and preserve every script's golden
function. Do not weaken tolerances or replace a device run with compile-only
unless the inventory explicitly classifies it that way.

Run one bounded process per case. Two-device entries require a healthy supported
pair; if HCCL is environment-blocked, retain the terminal coverage row and rerun
the compile/export portion without claiming numerical validation.

The checked-in source-oriented corpus includes the compile/export stage of this
workflow. Host capture provides solver inputs immediately, but it does not
replace the device-correct acceptance required by this section.

## 5. Normalize and audit

Import PyPTO and PyPTO-Lib raw trees separately with `dsa-corpus`, exact source
and producer commits, and their coverage TSVs. Acceptance requires:

- no missing, unexpected, or unlisted case;
- all schema-v1 documents validate;
- all new compiler exports use `pypto_hard_v1` unless an explicitly listed
  research feature requires `pypto_research_v1`;
- every hard document has one interval and one fixed pool per buffer;
- duplicate target/problem shapes have one representative;
- allocation-trivial observations remain in `manifest.tsv` but are not selected;
- every selected document has a source path, family, raw fingerprint, canonical
  fingerprint, and explicit selection reason.

Report raw observations, unique shapes, selected meaningful instances, and
counts by family, profile, pool, buffers, conflict density, reuse candidates,
alias width, pipeline groups, and depth shedding.

## 6. Solver suite

Run `dsa-suite` with:

```bash
--standard third_party/minimalloc/benchmarks/challenging
--pypto <normalized-pypto>/instances
--pypto <normalized-pypto-lib>/instances
--seeds 0,1,2
--iterations 2000
--restarts 4
--minimalloc-timeout-ms 60000
```

Require every returned placement to pass independent validation. The standard
table must contain MiniMalloc exact, first fit, XLA heap, TVM hill climb, and
generic local search for public standard instances and generated core
relaxations. The PyPTO table must contain every compatible heuristic and keep
hard/research profiles visible. Unsupported cells remain `unsupported`; they
must never be populated from a relaxation.

## 7. Deliverables

Preserve:

1. pin, environment, and device-health report;
2. terminal execution row for every inventory entry;
3. raw corpora and source-to-export index;
4. normalized `manifest.tsv` and `coverage.tsv`;
5. `results.jsonl`, `summary.csv`, `features.csv`, and `report.md`;
6. golden failures and paired PyPTO/DSA classification logs; and
7. a proposed diff under `benchmarks/pypto/instances` containing only unique meaningful
   device-correct instances.
