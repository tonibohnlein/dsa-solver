# PyPTO exported corpus

These schema-v1 documents are byte-for-byte outputs of PyPTO's pre-memory-reuse
DSA exporter. They are compiler instances, not manually reduced standard DSA
problems.

| Instance | PyPTO source | Regression guarded | First-fit peak |
| --- | --- | --- | ---: |
| `chain_read_before_write_v1.json` | `test_dsa_export_is_deterministic_pypto_structured` | Reads at a statement precede its result write, so a chain may reuse one slot | 16,384 B |
| `issue_1908_fragmentation_v1.json` | `test_dsa_pypto_profile_reuses_whole_freed_slots` | PyPTO reuses one whole 64 KiB slot and keeps the other live 32 KiB buffer disjoint | 98,304 B |
| `pipeline_stage_separation_v1.json` | `test_dsa_export_and_solver_preserve_pipeline_stage_separation` | Disjoint pipeline stages remain separated despite non-overlapping lifetimes | 32,768 B |
| `target_hazard_v1.json` | `test_dsa_export_preserves_ascend910b_target_hazard_reason` | The Ascend910B split-AIV load+tpop keep-apart edge retains target-hazard provenance | 8,192 B |
| `capacity_gated_pipeline_cost_v1.json` | `test_dsa_export_preserves_capacity_gated_pipeline_reuse_cost` | Capacity folds three stages into one residue and exports two sparse cross-pipe reuse costs | 245,760 B |

Run any instance directly:

```bash
./build/dsa-bench \
  --input benchmarks/pypto/issue_1908_fragmentation_v1.json \
  --solver first-fit
```

The source test name is the regeneration contract. Regenerate a document with
PyPTO's `MemoryPlanner.DSA` and `dsa_export_dir`, review the schema diff, then
replace the corresponding corpus file. CMake tests parse, validate, solve, and
independently validate every document listed above.
