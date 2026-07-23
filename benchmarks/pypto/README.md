# PyPTO corpus

This directory contains 273 unique schema-v1 problems:

| Source | Base captures | DSA-RP pairs | Files |
| --- | ---: | ---: | ---: |
| System tests | 161 | 54 | 269 |
| Memory-planning fixtures | 4 | 0 | 4 |

Paths preserve the source suite and program. Programs with one captured kernel
use `<program>__<kernel>.json`; directories are kept when a program has several
instances.

Each DSA-RP pair differs only in how the same mechanically recognized
cross-pipe relations are modeled: hard separations or soft unit penalties.

Each JSON file contains the solver profile, pools, buffers, lifetimes,
constraints, target, and source/exporter revisions. Structurally duplicate
captures are stored once, including duplicates shared with PyPTO-Lib.

Files under `../capture/` specify source coverage for corpus regeneration; they
are not benchmark instances. Host solver validation also does not establish
device numerical correctness or performance.

See [`../README.md`](../README.md) for corpus conventions and
[`../results/standard-v1/report.md`](../results/standard-v1/report.md) for the
standard-DSA comparison.
