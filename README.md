# dsa-solver

A standalone C++17 framework for Dynamic Storage Allocation (DSA): assign fixed-lifetime buffers to
memory offsets while minimizing peak usage and respecting compiler constraints.

The project has two roles:

1. provide reproducible solver benchmarks against the public Google MiniMalloc corpus and exact solver;
2. become the solver library used by PyPTO's memory-planning adapter once the API stabilizes.

The repository is initially private. No redistribution license has been selected yet; choose one before
making it public.

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
git clone --recurse-submodules git@github.com:tonibohnlein/dsa-solver.git
cd dsa-solver
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

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

## Compare with MiniMalloc

Build the pinned exact solver, generate a solution, then pass that output to this runner:

```bash
cmake -S third_party/minimalloc -B third_party/minimalloc/build -DCMAKE_BUILD_TYPE=Release
cmake --build third_party/minimalloc/build --parallel 2 --target minimalloc

third_party/minimalloc/build/minimalloc \
  --capacity=12 \
  --input=third_party/minimalloc/benchmarks/examples/input.12.csv \
  --output=minimalloc.csv

./build/dsa-bench \
  --input third_party/minimalloc/benchmarks/examples/input.12.csv \
  --solver local-search \
  --reference-output minimalloc.csv
```

MiniMalloc is a capacity-feasibility solver. Finding a certified minimum height requires repeated exact
solves over capacity; suite-level capacity search and timeout accounting are planned benchmark-runner
work rather than silently treating a feasible reference capacity as optimal.

The challenging corpus is available at `third_party/minimalloc/benchmarks/challenging/` as A–K. The
current CLI operates on one instance per invocation and emits machine-readable JSON, making shell or CI
orchestration straightforward while the native suite runner is developed.

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

1. Add suite-level repeated runs, exact-capacity search, timeouts, and JSONL/CSV aggregation.
2. Grow the exported PyPTO corpus across models, memory pools, control flow, and pipeline shapes.
3. Model capacity-driven pipeline-depth choices rather than only fixed pairwise separations.
4. Add idealloc and additional heuristic baselines.
5. Implement placement-aware large-neighborhood search with reproducible ablations against the TVM policy.

See [the TVM hill-climb study](docs/tvm_hill_climb.md) for the exact search state, neighborhood,
deliberate compatibility fixes, and the PyPTO refinement path.
