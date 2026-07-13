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
- A seeded iterated local-search baseline over first-fit placement orderings.
- A named reimplementation of Apache TVM USMP's graph-guided hill-climb policy.
- Native MiniMalloc input/output CSV support (`id,lower,upper,size[,offset]`).
- A `dsa-bench` CLI with JSON results and reference-solution comparison.
- A native `dsa-suite` runner that executes repeated heuristic and exact MiniMalloc runs, independently
  validates solutions, and writes raw JSONL, aggregated CSV, and Markdown tables.
- Versioned structured JSON for replaying compiler instances without compiler IR dependencies.
- A checked-in corpus of byte-for-byte PyPTO exporter outputs, replayed by all built-in solvers in CTest.
- Explicit standard, PyPTO-structured, and sound core-relaxation benchmark profiles.
- Central solver capability matching for hard features and requested objective terms.
- Google MiniMalloc pinned as a submodule, including the official A–K corpus and exact C++ solver.
- CMake install/export support for later `find_package` or `add_subdirectory` use from PyPTO.

The ordering local search is a research baseline, not yet the intended novel placement-aware algorithm.
The next solver will add direct offset/region moves and bounded backtracking behind the same `DsaSolver`
interface.

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
  --iterations 20000
```

Run the explicitly relaxed standard-DSA lower bound for one source pool:

```bash
./build/dsa-bench \
  --input tests/data/pypto_structured_v1.json \
  --core-relaxation-pool 3 \
  --solver first-fit
```

The relaxation strips compiler constraints and records each removed feature in the result. It is a lower
bound, not a valid PyPTO placement. See [the schema-v1 contract](docs/structured_problem_schema_v1.md).

## Run reproducible benchmark suites

`dsa-suite` accepts repeatable files or directories. This command runs the official MiniMalloc A–K
corpus, the checked-in PyPTO exporter corpus, three seeds for stochastic methods, and the exact
MiniMalloc solver with a per-instance timeout:

```bash
./build/dsa-suite \
  --standard third_party/minimalloc/benchmarks/challenging \
  --pypto benchmarks/pypto \
  --output-dir benchmark-results \
  --run-label local-a-k \
  --standard-capacity 1048576 \
  --seeds 0,1,2 \
  --iterations 2000 \
  --restarts 4 \
  --minimalloc-timeout-ms 60000
```

The output directory contains:

- `results.jsonl`: one immutable record per instance, method, and seed;
- `summary.csv`: long-form per-method aggregation with best objective and median runtime;
- `report.md`: separate standard-DSA and PyPTO-structured comparison tables.

Raw records distinguish `placement_valid` from `solution_valid`. The former validates address geometry
while ignoring capacity only for a `best_effort_no_fit` diagnostic placement; the latter always validates
the original problem, including pool capacities.

The runner invokes MiniMalloc's capacity-minimization mode. A completed run is marked `optimal`; a
budget exhaustion is marked `timeout` or `timeout_with_upper_bound` and is never used as a certified
optimality gap. On PyPTO inputs, MiniMalloc runs only on generated per-pool core relaxations. Those
results are reported as lower bounds only when the projection is sound and the relaxation optimum is
certified, never as valid structured placements. Schema v1 declines the lower-bound column for temporal
exclusions, flexible pool assignment, colocations, or overlapping intervals within one buffer. See the
checked-in [baseline snapshot](benchmarks/results/baseline/report.md).

## Model boundary

MiniMalloc CSV uses half-open lifetimes `[lower, upper)`. PyPTO records statement-level definition and
last-use points with reads ordered before writes at one statement. Its adapter expands statement `p` into
read event `2p` and write event `2p+1`, then exports a definition at `2*def+1` and a final-read end at
`2*last_use+1` (or one write event for an otherwise-unused definition). This preserves safe same-statement
input/result reuse without changing the portable half-open model.

The core model intentionally carries more structure than MiniMalloc CSV can encode:

| Structure | Representation | First-fit | Local search | TVM hill climb |
| --- | --- | --- | --- | --- |
| Multi-interval liveness | `Buffer::live_intervals` | yes | yes | yes |
| Fixed multi-pool planning | `Pool`, `Buffer::allowed_pools` | yes | yes | yes |
| Flexible pool assignment | multiple allowed pools | no | no | no |
| Must-alias | `Colocation` | yes | yes | yes |
| Pipeline/hazard separation | `Separation` | yes | yes | yes |
| Branch/phi exclusivity | `TemporalExclusion` | yes | yes | yes |
| Pinned allocation | `PinnedAllocation` | yes | yes | yes |
| Reserved address holes | `Pool::reserved_ranges` | yes | yes | yes |
| Reuse/synchronization cost | `CostModel::reuse_penalties` | reported baseline | optimized | optimized |
| Bank geometry/cost | `Pool::bank_geometry` | represented only | represented only | represented only |

Separation reasons and `PyptoStructure` are provenance: solvers enforce the translated generic constraints
and costs. They are retained so the planned `pypto-structured-search` can introduce alias-class and
pipeline-group neighborhoods without changing the portable core.

Every solver advertises capabilities. Unsupported hard structure produces `kUnsupported`; it is not
silently dropped. Objective-only mismatches are reported separately: first-fit can remain a disclosed
structural baseline, while a search solver rejects metrics it cannot use for candidate ranking.

## Next benchmark milestones

1. Grow the exported PyPTO corpus across models, memory pools, control flow, and pipeline shapes.
2. Record public `pypto-lib` instances with source revisions, target, pool, and content hashes.
3. Model capacity-driven pipeline-depth choices rather than only fixed pairwise separations.
4. Add idealloc and additional heuristic baselines through the same result contract.
5. Implement placement-aware large-neighborhood search with reproducible ablations against the TVM policy.

See [the TVM hill-climb study](docs/tvm_hill_climb.md) for the exact search state, neighborhood,
deliberate compatibility fixes, and the PyPTO refinement path.
