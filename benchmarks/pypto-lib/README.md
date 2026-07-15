# PyPTO-Lib corpus

This directory contains 289 unique schema-v1 problems:

| Source | Instances |
| --- | ---: |
| Examples | 11 |
| DeepSeek v3.2/v4 | 165 |
| Qwen3 14B/32B | 113 |

Paths preserve the source model and program. Programs with one captured kernel
use `<program>__<kernel>.json`; directories are kept when a program has several
instances. Metadata records the exact source path and producer/source
revisions.

The inventory in `../capture/pypto-lib-6e897cd.tsv` is a coverage specification,
not a solver input. See [`../README.md`](../README.md) for corpus conventions and
[`../results/standard-v1/report.md`](../results/standard-v1/report.md) for the
combined standard-DSA comparison.
