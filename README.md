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
- Optional reuse penalties for PyPTO's reuse-to-synchronization cost overlay.
- An independent problem/solution validator and objective recomputation.
- A deterministic decreasing-size first-fit baseline with lifetime-aware hole reuse.
- A seeded iterated local-search baseline over first-fit placement orderings.
- Native MiniMalloc input/output CSV support (`id,lower,upper,size[,offset]`).
- A `dsa-bench` CLI with JSON results and reference-solution comparison.
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
```

The CLI writes one JSON record to stdout. Important fields are `peak`, `runtime_us`, `status`, and—when a
reference output is supplied—`reference_peak`, `gap_bytes`, and `gap_percent`.

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

MiniMalloc CSV uses half-open lifetimes `[lower, upper)`. PyPTO currently records inclusive
`[definition, last_use]` points, so its adapter must convert `upper = last_use + 1` with overflow checking.

The core model intentionally carries more structure than MiniMalloc CSV can encode:

| Structure | Representation | First-fit | Local search |
| --- | --- | --- | --- |
| Multi-interval liveness | `Buffer::live_intervals` | yes | yes |
| Fixed multi-pool planning | `Pool`, `Buffer::allowed_pools` | yes | yes |
| Flexible pool assignment | multiple allowed pools | no | no |
| Must-alias | `Colocation` | yes | yes |
| Pipeline/hazard separation | `Separation` | yes | yes |
| Branch/phi exclusivity | `TemporalExclusion` | yes | yes |
| Pinned allocation | `PinnedAllocation` | yes | yes |
| Reserved address holes | `Pool::reserved_ranges` | yes | yes |
| Reuse/synchronization cost | `CostModel::reuse_penalties` | measured | optimized in fit-cost mode |
| Bank geometry/cost | `Pool::bank_geometry` | represented only | represented only |

Every solver advertises capabilities. Unsupported structure produces `kUnsupported`; it is not silently
dropped.

## Planned benchmark milestones

1. Pin MiniMalloc and ingest its `benchmarks/challenging` A–K corpus.
2. Add suite-level repeated runs, exact-capacity search, timeouts, and JSONL/CSV aggregation.
3. Export real PyPTO allocation problems into the same model.
4. Add lower-bound and additional heuristic baselines.
5. Implement placement-aware large-neighborhood search with reproducible ablations.
