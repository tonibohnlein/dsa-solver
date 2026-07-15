# PyPTO-Lib DSA instances

This directory contains 294 unique schema-v1 DSA problems captured from
PyPTO-Lib examples and models:

```text
benchmarks/pypto-lib/
├── examples/{advanced,beginner,intermediate}/
└── models/
    ├── deepseek/{v3_2,v4}/
    └── qwen3/{14b,32b}/
```

A program directory is retained only when it contains multiple instances.
Single-instance programs use `<program>__<kernel>.json` in the parent directory.
The JSON metadata records the exact source path and producer/source revisions.

Run this corpus together with the PyPTO corpus using:

```bash
./build/dsa-suite \
  --pypto benchmarks/pypto \
  --pypto benchmarks/pypto-lib \
  --output-dir benchmark-results \
  --run-label local-pypto
```

Capture inventories in `benchmarks/capture/` are coverage specifications, not
solver inputs.
