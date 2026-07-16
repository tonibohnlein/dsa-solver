# dsa-solver

A standalone C++17 framework for Dynamic Storage Allocation (DSA): assign fixed-lifetime buffers to
memory offsets while minimizing peak usage and respecting compiler constraints.

The project has two roles:

1. provide reproducible solver benchmarks against the public Google MiniMalloc corpus and exact solver;
2. provide a stable problem, validator, and ablation framework from which a winning heuristic can be
   ported into PyPTO without moving compiler-specific adapter logic out of PyPTO.

The project is licensed under [Apache License 2.0](LICENSE). See [NOTICE](NOTICE) and
[third-party notices](THIRD_PARTY_NOTICES.md) for algorithm and submodule provenance.

## Current contents

- A portable problem model with half-open, multi-interval lifetimes.
- Multiple fixed memory pools, capacities, reserved address ranges, and alignment.
- Must-alias colocations, keep-apart separations, control-flow temporal exclusions, and pins.
- Optional reuse penalties plus normalized PyPTO alias and pipeline-group provenance.
- An independent problem/solution validator and objective recomputation.
- A deterministic decreasing-size first-fit baseline with lifetime-aware hole reuse.
- A frozen OpenXLA spatial decreasing-size/best-fit heap baseline.
- A seeded iterated local-search baseline over first-fit placement orderings.
- A named reimplementation of Apache TVM USMP's graph-guided hill-climb policy.
- A PyPTO-structured search with pipeline-block, semantic-alias, and reuse-cost neighborhoods.
- Native MiniMalloc input/output CSV support (`id,lower,upper,size[,offset]`).
- A `dsa-bench` CLI with JSON results and reference-solution comparison.
- A native `dsa-suite` runner that executes repeated heuristic and exact MiniMalloc runs, independently
  validates solutions, and writes raw JSONL, aggregated CSV, and Markdown tables.
- A native `dsa-corpus` importer that turns raw compiler export trees into uniquely identified,
  provenance-rich benchmark corpora and fails closed on missing coverage targets.
- A native `dsa-bind` architecture binder with versioned Ascend 910B/950 resource specifications,
  lowering-ABI checks, and stable program/architecture fingerprints.
- Versioned structured JSON for replaying compiler instances without compiler IR dependencies.
- A checked-in corpus of byte-for-byte PyPTO exporter outputs, replayed by all built-in solvers in CTest.
- Explicit standard, PyPTO hard-v1, PyPTO research-v1, legacy structured, and sound core-relaxation profiles.
- Central solver capability matching for hard features and requested objective terms.
- Google MiniMalloc pinned as a submodule, including the official A–K corpus and exact C++ solver.
- CMake install/export support for later `find_package` or `add_subdirectory` use from PyPTO.

The ordering local search is a research baseline, not yet the intended novel placement-aware algorithm.
The next solver will add direct offset/region moves and bounded backtracking behind the same `DsaSolver`
interface.

See the [documentation index](docs/README.md), especially the consolidated
[PyPTO DSA definition](docs/pypto_dsa.md).

## Build and test

```bash
git clone --recurse-submodules https://github.com/tonibohnlein/dsa-solver.git
cd dsa-solver
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

The exact MiniMalloc baseline is enabled by default and compiles its unmodified core from the pinned
submodule. Configure with `-DDSA_ENABLE_MINIMALLOC_BASELINE=OFF` to build the suite/report machinery
without that baseline.

## Run a MiniMalloc instance

```bash
./build/dsa-bench \
  --input tests/data/minimalloc_example.csv \
  --solver first-fit \
  --output first-fit.csv

./build/dsa-bench \
  --input tests/data/minimalloc_example.csv \
  --solver xla-heap

./build/dsa-bench \
  --input tests/data/minimalloc_example.csv \
  --solver local-search \
  --capacity 12 \
  --seed 7 \
  --iterations 20000 \
  --restarts 8 \
  --output local-search.csv

./build/dsa-bench \
  --input tests/data/minimalloc_example.csv \
  --solver tvm-hill-climb \
  --capacity 12 \
  --seed 7 \
  --iterations 500 \
  --target-total-peak 12
```

The CLI writes one JSON record to stdout. Important fields are `peak`, `runtime_us`, `status`, the search
budget/options, and—when a reference output is supplied—`reference_peak`, `gap_bytes`, and `gap_percent`.
Results also identify the benchmark profile, objective vector, required features, and any capability
mismatch.

## Run a structured compiler instance

Schema-v1 JSON carries the full portable problem, including pools, multi-interval liveness, hard
constraints, pins, cost overlays, normalized PyPTO structure, and a lexicographic objective:

```bash
./build/dsa-bench \
  --input tests/data/pypto_structured_v1.json \
  --solver local-search \
  --seed 7 \
  --iterations 20000 \
  --solution-output placement.dsa.solution.json
```

The solution artifact is fingerprinted against the complete structured input
and can be independently validated and replayed by a compiler adapter.

Run the explicitly relaxed standard-DSA lower bound for one source pool:

```bash
./build/dsa-bench \
  --input tests/data/pypto_structured_v1.json \
  --core-relaxation-pool 3 \
  --solver first-fit
```

The relaxation strips compiler constraints and records each removed feature in the result. It is a lower
bound, not a valid PyPTO placement. See [the schema-v1 contract](docs/structured_problem_schema_v1.md).

## Bind a program to an architecture

An architecture-free PyPTO program uses the regular structured-problem JSON,
but leaves every pool capacity `null`, omits target metadata, and declares a
`metadata.lowering_abi`. `dsa-bind` combines it with a versioned architecture
specification and writes an ordinary solver input:

```bash
./build/dsa-bind \
  --program tests/data/pypto_unbound_program_v1.json \
  --architecture benchmarks/architectures/ascend910b-v1.json \
  --output /tmp/program-ascend910b.json

./build/dsa-bench \
  --input /tmp/program-ascend910b.json \
  --solver first-fit
```

Binding supplies usable capacity, minimum alignment, reserved ranges, and
optional bank geometry. It fails if the architecture lacks a required logical
space or does not support the program's lowering ABI. The output records stable
program and architecture fingerprints, allowing reports to compare a genuine
`(lowered program, architecture)` pair without treating a capacity edit as a
new compiler capture. See [the binding contract](docs/architecture_binding.md).

## Run reproducible benchmark suites

`dsa-suite --standard-only` compares algorithms on capacity-free standard DSA.
It uses MiniMalloc inputs directly and derives independent per-pool standard
projections from the checked-in PyPTO corpus. Trivial and duplicate projections
are omitted:

```bash
./build/dsa-suite \
  --standard third_party/minimalloc/benchmarks/challenging \
  --pypto benchmarks/pypto \
  --pypto benchmarks/pypto-lib \
  --output-dir benchmark-results \
  --run-label local-standard \
  --seeds 0,1,2 \
  --iterations 2000 \
  --restarts 4 \
  --deterministic-repetitions 5 \
  --minimalloc-timeout-ms 5000 \
  --standard-only
```

The output directory contains:

- `results.jsonl`: one immutable record per instance, method, and seed;
- `summary.csv`: long-form best-peak and median-runtime aggregation;
- `report.md`: compact per-corpus solution-quality and first-fit-normalized
  runtime aggregates.

Presentation-only changes do not require rerunning solvers. Rebuild the report
from the existing raw results by appending `--report-only` to the recorded
suite command.

## Import compiler model corpora

Raw PyPTO exports use function-local names such as `kernel`, so concatenating model runs directly can
create duplicate benchmark identities. `dsa-corpus` normalizes an export tree without changing its DSA
problem, deduplicates repeated target/problem shapes without losing source observations, attaches the
exact source repository/commit/path and raw-file fingerprint, and writes `manifest.tsv` plus
`coverage.tsv`. Unique but allocation-trivial shapes remain auditable in the manifest and are not
weighted as solver benchmarks:

```bash
./build/dsa-corpus \
  --input device-regression-artifacts/corpus \
  --output /tmp/pypto-lib-corpus \
  --coverage-targets benchmarks/capture/pypto-lib-6e897cd.tsv \
  --source-repo https://github.com/hw-native-sys/pypto-lib.git \
  --source-commit 6e897cd99c28767b22e05f209da3e041f15c3dfc \
  --producer-repo https://github.com/tonibohnlein/pypto.git \
  --producer-commit 8df2ed4bc56d73a9db434f42a6c6fe937dcb08d1 \
  --namespace pypto-lib
```

The current target contract inventories all 61 discovered entry points: 58 must
produce DSA documents and three are explicitly excluded. Two exclusions have no
Ascend InCore DSA problem; the third still uses the `auto_chunk` API removed from
the pinned PyPTO revision and cannot currently compile. DeepSeek v3.2/v4 and
Qwen3 14B/32B are exhaustive at the pinned revision. Import fails if a capture
target is missing, an excluded target produces a document, or an unlisted case appears. See
[the corpus workflow](docs/compiler_corpus.md).

The checked-in corpus stores normalized JSON directly under
`benchmarks/pypto` and `benchmarks/pypto-lib`, organized by source program. The
two directories contain 452 unique meaningful problems after structural
deduplication and removal of no-choice instances.

The complete per-instance size, lifetime, memory-space, capacity-pressure, and
structured-constraint inventory is checked in as
[`benchmarks/corpus.csv`](benchmarks/corpus.csv); column definitions and the
UB/L1/L0 mapping are in [`benchmarks/README.md`](benchmarks/README.md).

Do not import the earlier 597-document `b8802dc6` regression archive as a
published benchmark. That run was essential for finding the DeepSeek-v4
lifetime-hole defect, but some exported lifetimes are now known to be unsound.
Regenerate every case with the fixed producer revision above, then apply
structural deduplication and meaningful-instance selection.

Raw records distinguish `placement_valid` from `solution_valid`. The former validates address geometry
while ignoring capacity only for a `best_effort_no_fit` diagnostic placement; the latter always validates
the original problem, including pool capacities.

The standard-only runner invokes MiniMalloc's capacity-minimization mode using
the sum of buffer sizes as an initial safe upper bound. A completed run is
marked `optimal`; a budget exhaustion is marked `timeout` or
`timeout_with_upper_bound` and is never reported as certified.

## Source layout

Public headers live beside their implementations under `src/dsa/`:

```text
src/dsa/
  model/       problem representation, structured format, validation
  algorithms/  solver interface, placement engine, and solver implementations
  io/          MiniMalloc CSV interchange
```

New code should include structured paths such as
`dsa/model/structured_problem.h` and
`dsa/algorithms/local_search_solver.h`. CMake also generates and installs the
original flat `dsa/*.h` paths as compatibility headers, so existing consumers
such as the PyPTO adapter do not need an immediate source migration. The flat
paths contain no separate implementation or hand-maintained forwarding layer.

## Model boundary

MiniMalloc CSV uses half-open lifetimes `[lower, upper)`. PyPTO records statement-level definition and
last-use points with reads ordered before writes at one statement. Its adapter expands statement `p` into
read event `2p` and write event `2p+1`, then exports a definition at `2*def+1` and a final-read end at
`2*last_use+1` (or one write event for an otherwise-unused definition). This preserves safe same-statement
input/result reuse without changing the portable half-open model.

The core model intentionally carries more structure than MiniMalloc CSV can encode:

| Structure | Representation | First-fit | XLA heap | Local/TVM | PyPTO structured |
| --- | --- | --- | --- | --- | --- |
| Multi-interval liveness | `Buffer::live_intervals` | yes | no | yes | yes |
| Fixed multi-pool planning | `Pool`, `Buffer::allowed_pools` | yes | no | yes | yes |
| Flexible pool assignment | multiple allowed pools | no | no | no | no |
| Must-alias | `Colocation` | yes | yes | yes | yes |
| Pipeline/hazard separation | `Separation` | yes | no | yes | yes |
| Branch/phi exclusivity | `TemporalExclusion` | yes | no | yes | yes |
| Pinned allocation | `PinnedAllocation` | yes | no | yes | yes |
| Reserved address holes | `Pool::reserved_ranges` | yes | no | yes | yes |
| Reuse/synchronization cost | `CostModel::reuse_penalties` | reported | no | optimized | optimized + targeted moves |
| Bank geometry/cost | `Pool::bank_geometry` | represented | no | represented | represented |

Separation reasons and `PyptoStructure` are provenance: solvers enforce the translated generic constraints
and costs. `pypto-structured-search` consumes that provenance for alias-class, pipeline-block, and
reuse-penalty neighborhoods without changing the portable core.

Every solver advertises capabilities. Unsupported hard structure produces `kUnsupported`; it is not
silently dropped. Objective-only mismatches are reported separately: first-fit can remain a disclosed
structural baseline, while a search solver rejects metrics it cannot use for candidate ranking.

## Next benchmark milestones

1. Ingest the device-verified PyPTO-Lib exports against the checked-in exhaustive coverage target.
2. Extend the same explicit coverage contract to a curated PyPTO system-test/kernel inventory.
3. Model capacity-driven pipeline-depth choices rather than only fixed pairwise separations.
4. Add idealloc and additional heuristic baselines through the same result contract.
5. Implement placement-aware large-neighborhood search with reproducible ablations against the TVM policy.

See [the TVM hill-climb study](docs/tvm_hill_climb.md) for the exact search state, neighborhood,
deliberate compatibility fixes, and the PyPTO refinement path. See
[the XLA heap study](docs/xla_heap.md) for the second frozen compiler baseline.
