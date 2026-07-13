# Standard DSA regressions

These schema-v1 documents use the strict `standard_dsa` profile, so they are
directly comparable with fixed-size, single-interval state-of-the-art solvers.

`freed_region_subdivision_v1.json` is the minimal spatial-fragmentation shape:
buffer A occupies `[0, 100)` and expires; co-live buffers B and C then occupy
`[0, 60)` and `[60, 100)`. A solver that only reuses whole prior slots reaches
height 140 or rejects capacity 100, while a joint offset solver stays at 100.

Run it with:

```bash
./build/dsa-bench \
  --input benchmarks/standard/freed_region_subdivision_v1.json \
  --solver first-fit \
  --target-total-peak 100
```
