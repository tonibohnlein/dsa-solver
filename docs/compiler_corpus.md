# Compiler-derived corpus workflow

Raw compiler exports remain artifacts. Structurally normalized representatives
are checked into `benchmarks/pypto` and `benchmarks/pypto-lib`.

## Capture

PyPTO exports schema-v1 JSON before memory reuse. Coverage contracts in
`benchmarks/capture/` list every expected source entry point and reviewed
exclusion. `tools/capture_pypto_program.py` performs a host-only compile with
one process and one codegen worker; capture does not validate device numerics.

Raw outputs use one stable directory per source case because kernel-local names
are not globally unique.

## Normalize and select

`dsa-corpus`:

- validates every document;
- adds source/compiler repositories, commits, paths, and hashes;
- assigns source-qualified identities;
- canonicalizes non-semantic IDs and names;
- deduplicates canonical problem shapes; and
- writes complete observation and coverage manifests.

All solver-visible geometry, constraints, costs, aliases, and pipeline
provenance remain in the canonical fingerprint. Canonical bytes are compared
when fingerprints collide.

A capture is excluded from aggregate solver results only when it has no
placement choice: all buffers can share one address and no constraint, pin,
colocation, temporal exclusion, or reuse cost changes that conclusion. Excluded
observations remain in the manifest.

Current counts and statistics are generated in
[`benchmarks/corpus.csv`](../benchmarks/corpus.csv) and summarized in
[`benchmarks/README.md`](../benchmarks/README.md).

## Import

```bash
./build/dsa-corpus \
  --input /path/to/raw-corpus \
  --output /tmp/normalized-corpus \
  --coverage-targets benchmarks/capture/pypto-lib-6e897cd.tsv \
  --source-repo https://github.com/hw-native-sys/pypto-lib.git \
  --source-commit 6e897cd99c28767b22e05f209da3e041f15c3dfc \
  --producer-repo https://github.com/tonibohnlein/pypto.git \
  --producer-commit 8df2ed4bc56d73a9db434f42a6c6fe937dcb08d1 \
  --namespace pypto-lib
```

The output directory must be new or empty. Import fails for invalid input,
coverage mismatch, duplicate output identity, or already-normalized documents.

## Refresh checklist

For a new source or producer revision:

1. add a revision-named coverage TSV rather than editing the old contract;
2. capture and import into fresh directories;
3. require every coverage row to be captured or explicitly excluded;
4. compare manifests and corpus statistics;
5. verify that selected inputs use a device-validated, sound lifetime exporter;
6. independently validate all reported placements; and
7. update checked-in representatives deliberately.

Exact commits are the provenance authority; hashes detect accidental content
changes.
