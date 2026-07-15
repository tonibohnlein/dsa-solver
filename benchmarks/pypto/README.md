# PyPTO exported corpus

The root schema-v1 fixtures are byte-for-byte outputs of PyPTO's
pre-memory-reuse DSA exporter. They are compiler instances, not manually reduced
standard DSA problems. Larger model corpora live below `real/` after
normalization with `dsa-corpus`; their `problem` is exporter-derived while the
envelope gains globally unique identity and source provenance.

| Instance | PyPTO source | Regression guarded | First-fit peak |
| --- | --- | --- | ---: |
| `chain_read_before_write_v1.json` | `test_dsa_export_is_deterministic_pypto_hard_v1` | Reads at a statement precede its result write, so a chain may reuse one slot | 16,384 B |
| `issue_1908_fragmentation_v1.json` | `test_dsa_pypto_profile_reuses_whole_freed_slots` | PyPTO reuses one whole 64 KiB slot and keeps the other live 32 KiB buffer disjoint | 98,304 B |
| `pipeline_stage_separation_v1.json` | `test_dsa_export_and_solver_preserve_pipeline_stage_separation` | Disjoint pipeline stages remain separated despite non-overlapping lifetimes | 32,768 B |
| `target_hazard_v1.json` | `test_dsa_export_preserves_ascend910b_target_hazard_reason` | The Ascend910B split-AIV load+tpop keep-apart edge retains target-hazard provenance | 8,192 B |
| `capacity_gated_pipeline_cost_v1.json` | `test_dsa_export_preserves_capacity_gated_pipeline_reuse_cost` | Research profile: capacity folds three stages into one residue and exports two uncalibrated reuse costs | 245,760 B |

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

`targets/pypto_lib_bf89431.tsv` is the coverage contract for the first real
PyPTO-Lib corpus. It lists every tracked runnable entry point at commit
`bf89431fc73902caf594893888de84d06c3bf435`: 11 examples, 38 DeepSeek models,
and 7 Qwen3 models. Its `case_id` matches the device-regression artifact
directory. Importing with this target file fails on any missing or unknown case;
`coverage.tsv` records the realized document count per entry point.

`targets/pypto_b8802dc6.tsv` captures the real-device PyPTO kernel gates at the
adapter-fix revision: explicit planner smoke kernels, col-vector control flow,
gather whole-slot reuse, and depth-2 pipeline matmul. It complements the five
byte-for-byte unit-export fixtures rather than pretending four selected system
tests exhaust PyPTO's entire system-test suite.

The normalized document metadata preserves:

- exact source repository, commit, and Python entry point;
- original exporter instance and relative export filename;
- model family and case ID;
- FNV-1a fingerprints of the raw exporter bytes and canonical target/problem;
- the unchanged PyPTO producer, target, lifetime, and whole-slot contracts.

`manifest.tsv` retains one observation per source/kernel export. Structurally
identical target/problem shapes map to one representative JSON under
`documents/`, so aggregate solver tables do not count repeated model reuse as
independent evidence.

Run `dsa-suite --pypto <normalized-corpus>/documents ...` for solver reports and
per-instance `features.csv` constraint statistics.
See `docs/compiler_corpus.md` for ingestion and review rules.

## Checked-in real instances

`real/deepseek-v4-lifetime-hull-8438a916/` contains two structurally distinct
DeepSeek-v4 `softmax_pool` instances captured while proving the unsafe lifetime
hole fixed by the conservative-hull diagnostic revision. They are useful
benchmarks rather than merely failure artifacts: the instances contain 31 and
35 buffers, respectively, with hundreds of legal reuse candidates and
mandatory whole-slot alias structure. Both corrected instances retain the same
peak as the original PyPTO placement.

The workload sources are pinned to PyPTO-Lib `bf89431f`; `producer_commit`
records the exact diagnostic PyPTO exporter revision `8438a916`. That producer
commit is provenance for these artifacts, not a public PyPTO release. The
equivalent production fix is present on the draft PyPTO DSA branch and is
device-verified separately.

The known-unsafe pre-fix exports are deliberately not benchmark inputs. A
solver accepting their under-approximated lifetimes would only demonstrate
conformance to a bad problem statement.
