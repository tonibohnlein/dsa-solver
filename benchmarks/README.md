# Benchmarks

This directory contains the checked-in benchmark inputs and their provenance:

- `pypto/`: 273 PyPTO problems;
- `pypto-lib/`: 587 PyPTO-Lib problems;
- `minimalloc-dsa-rp/`: 11 synthetic scheduled-program DSA-RP lifts of
  MiniMalloc A–K;
- `architectures/`: target specifications used by the architecture binder;
- `capture/`: source-coverage inventories, not solver inputs;
- `corpus.csv`: one statistics row per PyPTO/PyPTO-Lib JSON input;
- `results/`: reproducible solver results.

## Structured corpus

Each JSON file is a schema-v1 `StructuredProblemDocument`. `corpus.csv`
summarizes the PyPTO/PyPTO-Lib captures: provenance, buffer sizes, lifetimes,
conflicts, capacities, and alias/pipeline structure. The synthetic MiniMalloc
lifts carry their statistics in document metadata and their result snapshot.
`uniform_buffer_size=true` identifies the unit-weighted DSA special case.

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

## MiniMalloc-derived DSA-RP

The A–K lifts are algorithmic stress tests, not reconstructed compiler traces.
Each original buffer keeps its size and lifetime and is given one synthetic
first write and final read on a deterministic four-stream schedule. The
`dsa-rp-lift` tool computes happens-before and emits a unit soft edge exactly
when a maximal access of the earlier buffer is unordered with the later
buffer's first write. Ordered handoffs remain free; no pipeline constraints or
hard/soft variants are added.

The lift uses an explicit quadratic pair scan as a simple offline reference.
It implements the Fable edge rule, not the engineering note's output-sensitive
compiler implementation.

Capacity is the deterministic standard first-fit peak, so every lifted instance
has a known fitting placement. The 11 lifts contain 154–454 buffers and
1,928–9,890 derived edges. Regenerate the flat A–K directory with:

```bash
./build/dsa-rp-lift \
  --input third_party/minimalloc/benchmarks/challenging \
  --output benchmarks/minimalloc-dsa-rp \
  --source-commit 9f5cf810fec4494df473c23cffd0567989e81b69 \
  --streams 4 \
  --seed 0 \
  --capacity-first-fit
```

The generator version, source revision, schedule seed, stream count, capacity
policy, compatible-pair count, and derived-edge count are stored in every JSON
document. These instances test the DSA-RP algorithms; they are not evidence for
any hardware synchronization cost.

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
