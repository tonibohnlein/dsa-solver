# PyPTO DSA instances

Every benchmark input is a schema-v1 JSON file in this directory. Directory
paths identify source suites and programs; capture method and revision are
provenance, not separate kinds of DSA problem. A program directory is retained
only when it contains multiple instances. A single-instance program is encoded
as `<program>__<kernel>.json` in its parent directory.

```text
benchmarks/pypto/
├── system-tests/{examples,runtime}/
└── unit-tests/memory-planning/
```

This directory contains 184 unique PyPTO problems:

| Source | Instances |
| --- | ---: |
| PyPTO system tests | 179 |
| PyPTO memory-planning unit fixtures | 5 |

Four system-test exports that are structurally identical to PyPTO-Lib model
instances are stored only under `benchmarks/pypto-lib/`. Repeated observations from the
same program are likewise represented once. This prevents benchmark results
from weighting a shared kernel multiple times.

## Instance format

Each `.json` file is a `StructuredProblemDocument` with:

- `schema_version`: currently `1`;
- `profile`: `pypto_hard_v1` or the experimental `pypto_research_v1`;
- `problem`: pools, buffers, sizes, alignments, live intervals, and PyPTO hard
  structure such as semantic alias classes and pipeline groups;
- `metadata`: target, lifetime ordering, reuse contract, and—on normalized
  compiler captures—the exact source/exporter repositories, commits, source
  path, raw-export fingerprint, and canonical problem fingerprint.

The JSON is the solver input. TSV files are not benchmark instances. Current
capture inventories live in `benchmarks/capture/`; `dsa-corpus` uses them to
check source coverage and emits a temporary manifest while normalizing raw
`*.dsa.json` exports.

## Running the corpus

```bash
./build/dsa-suite \
  --pypto benchmarks/pypto \
  --output-dir benchmark-results \
  --run-label local-pypto \
  --seeds 0,1,2 \
  --iterations 2000 \
  --restarts 4
```

The compiler-derived problems can be solved and validated on a host. That does
not by itself certify numerical device correctness or performance of the source
program; those are separate PyPTO/PyPTO-Lib regression campaigns.
