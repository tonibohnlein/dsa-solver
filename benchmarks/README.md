# Benchmarks

This directory contains the checked-in benchmark inputs and their provenance:

- `pypto/`: 273 PyPTO problems;
- `pypto-lib/`: 587 PyPTO-Lib problems;
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

| Origin | Base captures | DSA-RP pairs | Files | Buffers (min-max) | With reuse | With pipelines |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| PyPTO-Lib examples | 11 | 3 | 17 | 3-19 | 16 | 2 |
| PyPTO-Lib DeepSeek | 163 | 86 | 335 | 2-123 | 326 | 113 |
| PyPTO-Lib Qwen3 | 113 | 61 | 235 | 2-250 | 232 | 99 |
| PyPTO system tests | 161 | 54 | 269 | 2-66 | 232 | 31 |
| PyPTO unit fixtures | 4 | 0 | 4 | 2-4 | 4 | 2 |
| **Total** | **452** | **204** | **860** | **2-250** | **810** | **247** |

Each DSA-RP pair represents one unique recognized problem twice: `hard_v1`
turns the recognized cross-pipe pairs into separations, while `soft_v1` retains
them as unit reuse penalties. Edge-free captures are not duplicated. These
policies are experimental A/B inputs, not calibrated performance claims.

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
