# PyPTO DSA instances

Every benchmark input is a schema-v1 JSON file below `instances/`. The directory
path identifies the source repository and source program; capture method and
revision are provenance, not separate kinds of DSA problem.

```text
instances/
├── pypto-lib/
│   ├── examples/{advanced,beginner,intermediate}/
│   └── models/
│       ├── deepseek/{v3_2,v4}/
│       └── qwen3/{14b,32b}/
└── pypto/
    ├── system-tests/{examples,runtime}/
    └── unit-tests/memory-planning/
```

The checked-in corpus contains 478 unique problems:

| Source | Instances |
| --- | ---: |
| PyPTO-Lib examples | 11 |
| PyPTO-Lib DeepSeek models | 166 |
| PyPTO-Lib Qwen3 models | 117 |
| PyPTO system tests | 179 |
| PyPTO memory-planning unit fixtures | 5 |

Four system-test exports that are structurally identical to PyPTO-Lib model
instances are stored only under `pypto-lib/`. Repeated observations from the
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
  --pypto benchmarks/pypto/instances \
  --output-dir benchmark-results \
  --run-label local-pypto \
  --seeds 0,1,2 \
  --iterations 2000 \
  --restarts 4
```

The compiler-derived problems can be solved and validated on a host. That does
not by itself certify numerical device correctness or performance of the source
program; those are separate PyPTO/PyPTO-Lib regression campaigns.
