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
- `coverage.tsv` compares realized documents with the requested cases.

Every unique shape is classified before it is written. A representative is
selected when it contains pipeline groups, reuse costs, hard constraints,
multi-member semantic aliases, multiple intervals/pools, or at least four
buffers with both temporal conflicts and reuse candidates. Shapes without an
actual placement choice remain in `manifest.tsv` with
`selected=false, selection_reason=trivial_no_placement_choice`; they do not
enter aggregate solver results. This avoids both silent coverage loss and
benchmark inflation from allocation-trivial kernels.

Canonical target/problem shapes are fingerprinted after removing source-only
metadata and normalizing non-semantic pool, buffer, alias-member, and pipeline
group names. Buffer IDs and every solver-visible size, interval, edge, pool,
group membership, and objective remain part of the fingerprint. The first
observation in stable path order becomes the representative JSON under
`documents/`; later identical observations point to it from the manifest.
Fingerprint collisions are checked by comparing canonical bytes. This keeps
source coverage exhaustive without weighting a shared kernel shape once per
model that happens to reuse it.

The importer rejects inputs that are not `pypto_structured`, already-normalized
inputs, duplicate identities/output paths, unknown cases, invalid target paths,
and missing target counts. The output directory must be new or empty, preventing
stale documents from surviving a regeneration.

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

The case IDs for PyPTO-Lib commit `bf89431f` are pinned in
`benchmarks/pypto/targets/pypto_lib_bf89431.tsv`. That file is generated from
tracked runnable sources, never from ignored `build_output/` debug scripts.
The smaller `targets/pypto_b8802dc6.tsv` pins the already device-validated
PyPTO adapter/kernel gates; it is intentionally described as curated rather
than exhaustive.

## Import

```bash
./build/dsa-corpus \
  --input /path/to/device-regression-artifacts/corpus \
  --output /tmp/pypto-lib-corpus \
  --coverage-targets benchmarks/pypto/targets/pypto_lib_bf89431.tsv \
  --source-repo https://github.com/hw-native-sys/pypto-lib.git \
  --source-commit bf89431fc73902caf594893888de84d06c3bf435 \
  --producer-repo https://github.com/tonibohnlein/pypto.git \
  --producer-commit 1890b9e2aa92ea1f2e2a335d10190cc0f5bf1ad7 \
  --namespace pypto-lib
```

The 597 raw observations captured at PyPTO `b8802dc6` predate the sound
allocation-lifetime fix. Preserve them with the regression report, but do not
publish them as solver inputs: at least the affected DeepSeek-v4 `softmax_pool`
documents contain a false reusable lifetime hole. A corpus refresh must export
all targets from fixed commit `1890b9e2` (or a reviewed descendant) rather than
mixing producer revisions.

Review before checking in:

1. `coverage.tsv` has only `covered` rows and exactly 56 cases.
2. `manifest.tsv` covers every observation; representative instances and paths
   are unique, repeated shapes point to an existing representative, and every
   selected/skipped decision has an explicit reason.
3. Every document retains `producer=pypto`, `solver_input=pre_memory_reuse`, a
   non-empty target, and `whole_slot_reuse=true`.
4. `dsa-suite` reports every available heuristic feasible and
   placement-valid.
5. Large or redundant corpora are measured before committing; keep complete
   source coverage, but record repeated problem shapes rather than silently
   weighting aggregate results as independent evidence.

## Updating a source revision

Add a new target TSV whose filename includes the short source revision. Do not
overwrite an older coverage contract with a moving branch. Capture into a fresh
artifact root, import into a fresh normalized directory, compare manifests and
problem statistics, then update benchmark baselines deliberately.

The raw-byte fingerprint is a corruption/change detector, not a cryptographic
provenance signature. The exact Git commit and reviewed normalized JSON remain
the authoritative provenance.
