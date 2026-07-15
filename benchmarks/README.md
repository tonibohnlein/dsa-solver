# Benchmarks

This directory contains the checked-in benchmark inputs and their provenance:

- `pypto/`: 165 PyPTO problems;
- `pypto-lib/`: 287 PyPTO-Lib problems;
- `architectures/`: target specifications used by the architecture binder;
- `capture/`: source-coverage inventories, not solver inputs;
- `corpus.csv`: one statistics row per structured JSON input;
- `results/`: reproducible solver results.

## Structured corpus

Each JSON file is a schema-v1 `StructuredProblemDocument`. `corpus.csv`
summarizes provenance, buffer sizes, lifetimes, conflicts, capacities, and
PyPTO alias/pipeline structure. `uniform_buffer_size=true` identifies the
unit-weighted DSA special case.

The corpus omits captures with no placement choice: all buffers can share one
address and there is no separation, pinning, colocation, temporal exclusion,
or reuse-cost constraint. Zero temporal conflicts alone is not sufficient for
removal because PyPTO constraints may still matter.

PyPTO pools correspond to independent hardware memories:

| Pool | Space |
| --- | --- |
| `Vec` | UB |
| `Mat` | L1 |
| `Left` | L0A |
| `Right` | L0B |
| `Acc` | L0C |

`pool_capacities_bytes` and `max_interval_live_bytes_by_space` therefore retain
one value per pool. `max_live_capacity_ratio` is an unsolved pressure estimate,
not a placement peak.

## Corpus at a glance

| Origin | Instances | Buffers (min-max) | With reuse | With pipelines | Research profile |
| --- | ---: | ---: | ---: | ---: | ---: |
| PyPTO-Lib examples | 11 | 3-19 | 10 | 2 | 0 |
| PyPTO-Lib DeepSeek | 163 | 2-123 | 154 | 61 | 51 |
| PyPTO-Lib Qwen3 | 113 | 2-250 | 110 | 47 | 35 |
| PyPTO system tests | 161 | 2-66 | 124 | 3 | 0 |
| PyPTO unit fixtures | 4 | 2-4 | 4 | 2 | 1 |
| **Total** | **452** | **2-250** | **402** | **115** | **87** |

Research-cost rows use `pypto_research_v1`: production hard constraints plus
an experimental, uncalibrated reuse cost. The target mix is 451 Ascend 910B
captures and three Ascend 950 fixtures.

## Multi-pool and architecture binding

A scheduled kernel may contain several pools, especially matmul with L1, L0A,
L0B, and L0C. With fixed pool membership these are independent allocation
problems; `standard_dsa` comparisons project them into one problem per pool.
Flexible assignment or shared capacity would introduce cross-pool coupling.

The intended benchmark identity is:

```text
instance = lowered program variant + architecture specification
problem  = Bind(lowered program variant, architecture specification)
```

Architecture files supply usable capacities and alignment rules. Program
variants supply buffers, logical spaces, lifetimes, and constraints. A capture
cannot generally be retargeted by changing capacity alone because lowering and
tiling may be target-dependent. See
[`architecture_binding.md`](../docs/architecture_binding.md).
