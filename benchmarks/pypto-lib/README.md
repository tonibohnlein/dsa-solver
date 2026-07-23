# PyPTO-Lib corpus

This directory contains 587 unique schema-v1 problems:

| Source | Base captures | DSA-RP pairs | Files |
| --- | ---: | ---: | ---: |
| Examples | 11 | 3 | 17 |
| DeepSeek v3.2/v4 | 163 | 86 | 335 |
| Qwen3 14B/32B | 113 | 61 | 235 |

Paths preserve the source model and program. Programs with one captured kernel
use `<program>__<kernel>.json`; directories are kept when a program has several
instances. Metadata records the exact source path and producer/source
revisions.

Each DSA-RP pair contains the same recognized relations as hard separations and
as soft unit penalties. Edge-free captures remain single-copy.

The inventory in `../capture/pypto-lib-6e897cd.tsv` is a coverage specification,
not a solver input. See [`../README.md`](../README.md) for corpus conventions and
[`../results/standard-v1/report.md`](../results/standard-v1/report.md) for the
combined standard-DSA comparison.
