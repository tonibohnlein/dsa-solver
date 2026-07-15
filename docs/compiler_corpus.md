# Compiler-derived corpus workflow

Compiler instances are captured exhaustively, normalized structurally, and
then filtered for meaningful solver choices. Raw exports remain build/device
artifacts; normalized representatives are checked into `benchmarks/pypto` and
`benchmarks/pypto-lib`.

## Raw capture

PyPTO emits schema-v1 JSON before memory reuse. Each runnable source uses a
stable case directory because raw function-local names such as `kernel` are not
globally unique:

```text
corpus/
  models__deepseek__v4__decode_attention_swa/
    pypto_attention_aic.dsa.json
    pypto_attention_aiv.dsa.json
```

Coverage contracts under `benchmarks/capture/` enumerate every expected source
entry point and reviewed exclusion. `tools/capture_pypto_program.py` performs a
host-only compile with `MemoryPlanner.DSA`, one process, and one codegen worker.
This is sufficient to generate solver inputs, but it is not numerical device
validation.

## Normalization and selection

`dsa-corpus`:

- validates every PyPTO document;
- assigns a source-qualified instance identity;
- records source and producer repositories, commits, paths, and raw hashes;
- canonicalizes non-semantic IDs and names;
- deduplicates identical target/problem shapes; and
- writes `manifest.tsv` and `coverage.tsv` for the complete observations.

All solver-visible sizes, intervals, pools, edges, objectives, alias members,
and pipeline membership remain in the canonical fingerprint. Fingerprint
collisions are checked by comparing canonical bytes.

An observation is excluded from aggregate solver results when it has no actual
placement choice, for example no temporal conflicts. It remains visible in the
manifest. Unique instances with conflicts, reuse candidates, multiple pools,
pipeline groups, or semantic structure are retained even if current heuristics
solve them easily.

The checked-in corpus contains 454 meaningful, structurally deduplicated
problems: 165 from PyPTO and 289 from PyPTO-Lib. Their statistics are in
[`benchmarks/corpus.csv`](../benchmarks/corpus.csv).

## Import

```bash
./build/dsa-corpus \
  --input /path/to/raw-corpus \
  --output /tmp/pypto-lib-corpus \
  --coverage-targets benchmarks/capture/pypto-lib-6e897cd.tsv \
  --source-repo https://github.com/hw-native-sys/pypto-lib.git \
  --source-commit 6e897cd99c28767b22e05f209da3e041f15c3dfc \
  --producer-repo https://github.com/tonibohnlein/pypto.git \
  --producer-commit 8df2ed4bc56d73a9db434f42a6c6fe937dcb08d1 \
  --namespace pypto-lib
```

The output directory must be new or empty. Import fails for missing or
unexpected coverage targets, duplicate output identities, invalid paths,
non-PyPTO profiles, or already-normalized input.

Before publishing a refresh, require:

1. every inventory row is covered or explicitly excluded;
2. every observation appears in the manifest with a representative and reason;
3. every selected document retains target, producer, pre-memory-reuse input,
   sound lifetime, and whole-slot metadata;
4. all solver results pass independent placement validation; and
5. source coverage and unique problem counts are reported separately.

The earlier 597-document `b8802dc6` device archive must not be imported. It was
valuable for diagnosing the DeepSeek-v4 lifetime-hole defect, but contains
known-unsound lifetimes. All published inputs use the fixed exporter lineage.

## Updating revisions

Add a revision-named coverage TSV instead of modifying an old contract. Capture
and import into fresh directories, compare manifests and corpus statistics,
then update the checked-in representatives deliberately. Exact source and
producer commits are the provenance authority; hashes detect accidental
content changes.
