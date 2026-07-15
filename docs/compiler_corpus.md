# Compiler-derived corpus workflow

## Purpose

Compiler DSA instances are the research workload for PyPTO-aware search. They
must remain reproducible, attributable, and distinct from standard MiniMalloc
instances. This workflow converts raw PyPTO exporter trees into a corpus that
can be checked in and passed to `dsa-suite` as one directory.

## Raw and normalized layers

Raw `*.dsa.json` files are audit artifacts emitted by PyPTO before memory reuse.
They retain function-local names, so unrelated models may both export an
instance named `kernel`. Raw artifacts stay with the device/build report.

`dsa-corpus` writes a normalized layer:

- the DSA `problem`, profile, and schema version are parsed and reserialized;
- `instance` becomes `<namespace>::<source-path>::<export-stem>`;
- `corpus_*` metadata records source repository, exact commit, entry point,
  compiler/exporter repository and commit, original instance, export path,
  family, and raw-byte fingerprint;
- `manifest.tsv` indexes every source observation and its representative;
- `coverage.tsv` compares realized exports with the requested cases.

Every unique shape is classified before it is written. Pipeline groups, reuse
costs, and explicit hard constraints are always retained. Otherwise, a shape
with no temporal conflicts is allocation-trivial even if it has multiple fixed
pools or multi-member alias provenance: every buffer can start at address zero
in its pool. Among the remaining shapes, multi-member semantic aliases,
multiple intervals/pools, or at least four buffers with both temporal conflicts
and reuse candidates are retained. Shapes without an actual placement choice
remain in `manifest.tsv` with
`selected=false, selection_reason=trivial_no_placement_choice`; they do not
enter aggregate solver results. This avoids both silent coverage loss and
benchmark inflation from allocation-trivial kernels.

Canonical target/problem shapes are fingerprinted after removing source-only
metadata and normalizing non-semantic pool, buffer, alias-member, and pipeline
group names. Buffer IDs and every solver-visible size, interval, edge, pool,
group membership, and objective remain part of the fingerprint. The first
observation in stable path order becomes the representative JSON under
`instances/`; later identical observations point to it from the manifest.
Fingerprint collisions are checked by comparing canonical bytes. This keeps
source coverage exhaustive without weighting a shared kernel shape once per
model that happens to reuse it.

The importer rejects inputs that do not use a PyPTO profile, already-normalized
inputs, duplicate identities/output paths, unknown cases, invalid target paths,
and missing target counts. The output directory must be new or empty, preventing
stale instances from surviving a regeneration.

## Artifact layout

The device exporter should place each runnable source below a stable case ID:

```text
corpus/
  models__deepseek__v4__decode_attention_swa/
    pypto_attention_aic.dsa.json
    pypto_attention_aiv.dsa.json
  models__qwen3__14b__decode_layer/
    pypto_decode_layer.dsa.json
```

The case IDs for PyPTO-Lib commit `6e897cd9` are pinned in
`benchmarks/capture/pypto-lib-6e897cd.tsv`. That file inventories all 61
tracked entry points, never ignored `build_output/` debug scripts. An
extended target row records `eligibility=capture` with a positive minimum, or
`eligibility=exclude`, a zero minimum, and a reviewable reason. The current
inventory has 58 captures and three exclusions: a non-Ascend SuperscalarNPU
draft, an extern-only CCE driver with no InCore DSA allocation, and a Qwen3-32B
prefill draft that uses the removed `auto_chunk` API. The focused PyPTO adapter
gate inventory is `benchmarks/capture/pypto-adapter-gates-8df2ed4.tsv`; the
broad system-test inventory is
`benchmarks/capture/pypto-system-tests-8df2ed4.tsv`.

## Host-only capture

`tools/capture_pypto_program.py` loads one PyPTO-Lib entry point with its script
directory on `sys.path`, forces `MemoryPlanner.DSA`, selects a simulator target,
and stops after compiler code generation. It patches only the public golden
runner boundary; `--direct-pass-context` handles programs that compile without
that runner. Use one process and one codegen worker when sweeping the inventory.

The pinned host capture produced:

| Source | Raw observations | Unique shapes | Selected meaningful |
| --- | ---: | ---: | ---: |
| PyPTO-Lib entry points | 1,701 | 349 | 292 |
| PyPTO system tests | 461 | 225 | 183 |

The two selected sets share four canonical problems. The checked-in corpus keeps
the PyPTO-Lib model representative and omits the duplicate system-test JSON,
yielding 471 structured problems and 957 per-pool standard relaxations.

Host capture is suitable for solver benchmarking because solving and validating
the exported allocation problem does not execute an NPU program. It is not a
substitute for numerical device validation. Capture mode is not part of the
instance directory hierarchy; any claim about generated-program correctness
must come from the separate device campaign. Some PyPTO system tests compile through
the DSA export and then attempt an unavailable runtime path despite
`--codegen-only`; their emitted documents are compile-valid observations, not
whole-test passes.

## Import

```bash
./build/dsa-corpus \
  --input /path/to/device-regression-artifacts/corpus \
  --output /tmp/pypto-lib-corpus \
  --coverage-targets benchmarks/capture/pypto-lib-6e897cd.tsv \
  --source-repo https://github.com/hw-native-sys/pypto-lib.git \
  --source-commit 6e897cd99c28767b22e05f209da3e041f15c3dfc \
  --producer-repo https://github.com/tonibohnlein/pypto.git \
  --producer-commit 8df2ed4bc56d73a9db434f42a6c6fe937dcb08d1 \
  --namespace pypto-lib
```

The 597 raw observations captured at PyPTO `b8802dc6` predate the sound
allocation-lifetime fix. Preserve them with the regression report, but do not
publish them as solver inputs: at least the affected DeepSeek-v4 `softmax_pool`
documents contain a false reusable lifetime hole. A corpus refresh must export
all targets from fixed commit `1890b9e2` (or a reviewed descendant) rather than
mixing producer revisions.

Review before checking in:

1. `coverage.tsv` has exactly 61 rows: 58 `covered` and three reviewed `excluded`,
   with no `missing` or `unexpected` row.
2. `manifest.tsv` covers every observation; representative instances and paths
   are unique, repeated shapes point to an existing representative, and every
   selected/skipped decision has an explicit reason.
3. Every instance retains `producer=pypto`, `solver_input=pre_memory_reuse`, a
   non-empty target, and `whole_slot_reuse=true`.
4. `dsa-suite` reports every returned heuristic placement placement-valid.
   Capacity misses remain explicit `best_effort_no_fit` rows rather than being
   discarded or mislabeled feasible.
   Review `features.csv` and the report's feature-occurrence table; zero-count
   features cannot support structured-search claims.
   The standard table must include both public standard inputs and generated
   per-pool core relaxations; relaxation results remain lower bounds.
5. Large or redundant corpora are measured before committing; keep complete
   source coverage, but record repeated problem shapes rather than silently
   weighting aggregate results as independent evidence.

## Updating a source revision

Add a new target TSV under `benchmarks/capture/` whose filename includes the
short source revision. Do not
overwrite an older coverage contract with a moving branch. Capture into a fresh
artifact root, import into a fresh normalized directory, compare manifests and
problem statistics, then update benchmark baselines deliberately.

The raw-byte fingerprint is a corruption/change detector, not a cryptographic
provenance signature. The exact Git commit and reviewed normalized JSON remain
the authoritative provenance.
