#!/usr/bin/env python3
# Copyright 2026 dsa-solver contributors
# SPDX-License-Identifier: Apache-2.0

"""Build concise tables from a dsa-suite run over paired DSA-RP variants."""

from __future__ import annotations

import argparse
import csv
import json
import math
import statistics
from collections import defaultdict
from pathlib import Path
from typing import Any

METHODS = (
    ("Baseline", "first_fit", "First fit"),
    ("Baseline", "cypress_relaxation", "Cypress relaxation"),
    ("Constructive", "canonical_greedy", "Canonical greedy"),
    ("Constructive", "promote_repair", "Promote-repair"),
    ("Constructive", "promote_all", "Promote-all"),
    ("Local search", "reuse_penalty_local_search", "DSA-RP local search"),
    ("Exact", "canonical_branch_and_bound", "Canonical B&B"),
    ("Exact", "implicit_hitting_set", "Implicit hitting set"),
    ("Exact", "capacity_two_exact", "Capacity-two exact"),
    ("Exact", "span_one_min_cost_flow", "Span-one min-cost flow"),
    ("Exact", "treewidth_partition_dp", "Treewidth DP"),
    ("Exact", "reuse_penalty_portfolio", "Exact portfolio"),
    ("Bicriteria", "scale_separated_grid_dp", "Scale-separated grid DP"),
    ("Unit control", "unit_random_coloring", "Unit random coloring"),
    ("Unit control", "unit_low_rank_rounding", "Unit low-rank rounding"),
    ("Legacy search", "tvm_hill_climb", "TVM hill climb"),
    ("Legacy search", "local_search", "Local search"),
    ("Legacy search", "pypto_structured_search", "PyPTO structured search"),
)

HARD_METHODS = (
    ("first_fit", "First fit"),
    ("canonical_greedy", "Canonical greedy"),
    ("promote_repair", "Promote-repair"),
    ("cypress_relaxation", "Cypress relaxation"),
    ("tvm_hill_climb", "TVM hill climb"),
    ("local_search", "Local search"),
    ("pypto_structured_search", "PyPTO structured search"),
)

UNAVAILABLE = {"not_run", "unavailable", "unsupported"}
FEASIBLE = {"feasible", "optimal"}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("results_jsonl", type=Path)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--seeds", default="0,1,2")
    parser.add_argument("--iterations", type=int, default=2000)
    parser.add_argument("--restarts", type=int, default=4)
    parser.add_argument("--stagnation", type=int, default=250)
    return parser.parse_args()


def is_fit(record: dict[str, Any]) -> bool:
    return bool(record.get("solution_valid")) and record.get("status") not in UNAVAILABLE


def has_placement(record: dict[str, Any]) -> bool:
    return bool(record.get("placement_valid")) and record.get("peak") is not None


def objective(record: dict[str, Any]) -> tuple[int, ...]:
    return tuple(int(value) for value in record.get("objective_score", ()))


def record_rank(record: dict[str, Any]) -> tuple[Any, ...]:
    return (
        not (is_fit(record) and record.get("status") in FEASIBLE),
        not has_placement(record),
        objective(record),
        int(record.get("runtime_us", 0)),
    )


def median_runtime(records: list[dict[str, Any]]) -> int | None:
    runtimes = sorted(
        int(record["runtime_us"])
        for record in records
        if record.get("status") not in {"not_run", "unavailable"}
    )
    return runtimes[len(runtimes) // 2] if runtimes else None


def proof_present(records: list[dict[str, Any]]) -> bool:
    for record in records:
        metrics = record.get("solver_metrics", {})
        if int(metrics.get("optimality_proven", 0)) == 1:
            return True
        if int(metrics.get("optimal_reuse_cost_proven", 0)) == 1:
            return True
        if (
            int(metrics.get("zero_penalty_feasibility_proven", 0)) == 1
            and is_fit(record)
            and record.get("reuse_cost") == 0
        ):
            return True
    return False


def source_key(record: dict[str, Any]) -> str:
    metadata = record["metadata"]
    fingerprint = metadata.get("corpus_source_fingerprint_fnv1a64")
    if not fingerprint:
        raise ValueError(f"{record['instance']}: missing source fingerprint")
    return str(fingerprint)


def policy(record: dict[str, Any]) -> str:
    value = record["metadata"].get("dsa_rp_edge_policy")
    if value not in {"hard_v1", "soft_v1"}:
        raise ValueError(f"{record['instance']}: not a paired DSA-RP result")
    return str(value)


def namespace(record: dict[str, Any]) -> str:
    value = record["metadata"].get("corpus_namespace")
    return "PyPTO-Lib" if value == "pypto-lib" else "PyPTO"


def geometric_mean(values: list[float]) -> float | None:
    positive = [value for value in values if value > 0.0]
    if not positive:
        return None
    return math.exp(sum(math.log(value) for value in positive) / len(positive))


def percent(value: float | None) -> str:
    return "—" if value is None else f"{100.0 * value:.2f}%"


def ratio(value: float | None, count: int) -> str:
    return "—" if value is None else f"{value:.2f}x ({count})"


def load_records(path: Path) -> list[dict[str, Any]]:
    records = []
    with path.open(encoding="utf-8") as stream:
        for line_number, line in enumerate(stream, 1):
            if not line.strip():
                continue
            record = json.loads(line)
            try:
                policy(record)
                source_key(record)
            except (KeyError, ValueError) as error:
                raise ValueError(f"{path}:{line_number}: {error}") from error
            records.append(record)
    if not records:
        raise ValueError(f"{path}: no records")
    return records


def summarize_runs(
    records: list[dict[str, Any]],
) -> tuple[
    dict[tuple[str, str, str], dict[str, Any]],
    dict[tuple[str, str, str], list[dict[str, Any]]],
    dict[tuple[str, str], dict[str, Any]],
]:
    runs: dict[tuple[str, str, str], list[dict[str, Any]]] = defaultdict(list)
    documents: dict[tuple[str, str], dict[str, Any]] = {}
    for record in records:
        document_key = (policy(record), source_key(record))
        previous = documents.setdefault(document_key, record)
        if previous["instance"] != record["instance"]:
            raise ValueError(f"{document_key}: inconsistent instance identity")
        runs[(document_key[0], document_key[1], record["method"])].append(record)
    best = {key: min(group, key=record_rank) for key, group in runs.items()}
    return best, runs, documents


def validate_pairs(documents: dict[tuple[str, str], dict[str, Any]]) -> list[str]:
    keys = sorted({key for _, key in documents})
    for key in keys:
        if ("hard_v1", key) not in documents or ("soft_v1", key) not in documents:
            raise ValueError(f"{key}: missing hard/soft counterpart")
        hard = documents[("hard_v1", key)]
        soft = documents[("soft_v1", key)]
        hard_edges = int(hard["metadata"]["dsa_rp_cross_pipe_edges"])
        soft_edges = int(soft["metadata"]["dsa_rp_cross_pipe_edges"])
        if hard_edges != soft_edges:
            raise ValueError(f"{key}: hard/soft edge counts differ")
    return keys


def best_soft_costs(keys: list[str], best: dict[tuple[str, str, str], dict[str, Any]]) -> dict[str, int]:
    costs: dict[str, int] = {}
    for key in keys:
        candidates = [
            int(record["reuse_cost"])
            for (candidate_policy, candidate_key, _), record in best.items()
            if candidate_policy == "soft_v1"
            and candidate_key == key
            and is_fit(record)
            and record.get("reuse_cost") is not None
        ]
        if candidates:
            costs[key] = min(candidates)
    return costs


def best_hard_costs(keys: list[str], best: dict[tuple[str, str, str], dict[str, Any]]) -> dict[str, int]:
    costs: dict[str, int] = {}
    for key in keys:
        candidates = [
            int(record["reuse_cost"])
            for (candidate_policy, candidate_key, _), record in best.items()
            if candidate_policy == "hard_v1"
            and candidate_key == key
            and is_fit(record)
            and record.get("reuse_cost") is not None
        ]
        if candidates:
            costs[key] = min(candidates)
    return costs


def strict_fit_keys(keys: list[str], best: dict[tuple[str, str, str], dict[str, Any]]) -> set[str]:
    return {
        key
        for key in keys
        if any(
            is_fit(record)
            for (candidate_policy, candidate_key, _), record in best.items()
            if candidate_policy == "hard_v1" and candidate_key == key
        )
    }


def algorithm_rows(
    keys: list[str],
    best: dict[tuple[str, str, str], dict[str, Any]],
    runs: dict[tuple[str, str, str], list[dict[str, Any]]],
    documents: dict[tuple[str, str], dict[str, Any]],
    references: dict[str, int],
) -> list[dict[str, Any]]:
    rows = []
    for category, method, label in METHODS:
        applicable = fits = wins = zero = proofs = total_cost = 0
        active_fractions: list[float] = []
        runtime_ratios: list[float] = []
        for key in keys:
            record = best.get(("soft_v1", key, method))
            if record is None:
                continue
            if record["status"] not in UNAVAILABLE:
                applicable += 1
            if proof_present(runs[("soft_v1", key, method)]):
                proofs += 1
            if not is_fit(record) or record.get("reuse_cost") is None:
                continue
            fits += 1
            cost = int(record["reuse_cost"])
            total_cost += cost
            zero += cost == 0
            wins += references.get(key) == cost
            edges = int(documents[("soft_v1", key)]["metadata"]["dsa_rp_cross_pipe_edges"])
            active_fractions.append(cost / edges)
            first_fit = best.get(("soft_v1", key, "first_fit"))
            method_time = median_runtime(runs[("soft_v1", key, method)])
            first_fit_time = median_runtime(runs[("soft_v1", key, "first_fit")]) if first_fit else None
            if method_time and first_fit_time:
                runtime_ratios.append(method_time / first_fit_time)
        rows.append(
            {
                "category": category,
                "method": method,
                "label": label,
                "applicable": applicable,
                "fits": fits,
                "wins": wins,
                "zero": zero,
                "proofs": proofs,
                "total_cost": total_cost,
                "mean_active": statistics.fmean(active_fractions) if active_fractions else None,
                "runtime_ratio": geometric_mean(runtime_ratios),
                "runtime_instances": len(runtime_ratios),
            }
        )
    return rows


def hard_rows(
    keys: list[str],
    best: dict[tuple[str, str, str], dict[str, Any]],
    runs: dict[tuple[str, str, str], list[dict[str, Any]]],
) -> list[dict[str, Any]]:
    rows = []
    for method, label in HARD_METHODS:
        applicable = fits = 0
        runtime_ratios: list[float] = []
        for key in keys:
            record = best.get(("hard_v1", key, method))
            if record is None:
                continue
            if record["status"] not in UNAVAILABLE:
                applicable += 1
            if not is_fit(record):
                continue
            fits += 1
            method_time = median_runtime(runs[("hard_v1", key, method)])
            first_fit_time = median_runtime(runs[("hard_v1", key, "first_fit")])
            if method_time and first_fit_time:
                runtime_ratios.append(method_time / first_fit_time)
        rows.append(
            {
                "method": method,
                "label": label,
                "applicable": applicable,
                "fits": fits,
                "runtime_ratio": geometric_mean(runtime_ratios),
                "runtime_instances": len(runtime_ratios),
            }
        )
    return rows


def write_algorithm_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    fields = list(rows[0])
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def write_paired_csv(
    path: Path,
    keys: list[str],
    best: dict[tuple[str, str, str], dict[str, Any]],
    runs: dict[tuple[str, str, str], list[dict[str, Any]]],
    documents: dict[tuple[str, str], dict[str, Any]],
    references: dict[str, int],
) -> None:
    fields = (
        "source",
        "family",
        "source_path",
        "source_instance",
        "source_fingerprint",
        "edges",
        "strict_fit_found",
        "best_soft_solver_cost",
        "best_hard_solver_cost",
        "best_known_cost",
        "best_soft_methods",
        "optimum_proven",
    )
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, lineterminator="\n")
        writer.writeheader()
        for key in keys:
            soft = documents[("soft_v1", key)]
            strict_fit = any(
                is_fit(record)
                for (candidate_policy, candidate_key, _), record in best.items()
                if candidate_policy == "hard_v1" and candidate_key == key
            )
            reference = references.get(key)
            soft_cost = min(
                (
                    int(record["reuse_cost"])
                    for (candidate_policy, candidate_key, _), record in best.items()
                    if candidate_policy == "soft_v1"
                    and candidate_key == key
                    and is_fit(record)
                    and record.get("reuse_cost") is not None
                ),
                default=None,
            )
            hard_cost = min(
                (
                    int(record["reuse_cost"])
                    for (candidate_policy, candidate_key, _), record in best.items()
                    if candidate_policy == "hard_v1"
                    and candidate_key == key
                    and is_fit(record)
                    and record.get("reuse_cost") is not None
                ),
                default=None,
            )
            winning_methods = sorted(
                method
                for (candidate_policy, candidate_key, method), record in best.items()
                if candidate_policy == "soft_v1"
                and candidate_key == key
                and is_fit(record)
                and record.get("reuse_cost") == reference
            )
            proven = any(
                proof_present(group)
                and any(is_fit(record) and record.get("reuse_cost") == reference for record in group)
                for (candidate_policy, candidate_key, _), group in runs.items()
                if candidate_policy == "soft_v1" and candidate_key == key
            )
            metadata = soft["metadata"]
            writer.writerow(
                {
                    "source": namespace(soft),
                    "family": metadata.get("corpus_family", ""),
                    "source_path": metadata.get("corpus_source_path", ""),
                    "source_instance": metadata.get("dsa_rp_source_instance", ""),
                    "source_fingerprint": key,
                    "edges": metadata["dsa_rp_cross_pipe_edges"],
                    "strict_fit_found": str(strict_fit).lower(),
                    "best_soft_solver_cost": "" if soft_cost is None else soft_cost,
                    "best_hard_solver_cost": "" if hard_cost is None else hard_cost,
                    "best_known_cost": "" if reference is None else reference,
                    "best_soft_methods": ";".join(winning_methods),
                    "optimum_proven": str(proven).lower(),
                }
            )


def write_report(
    path: Path,
    keys: list[str],
    algorithm_stats: list[dict[str, Any]],
    strict_stats: list[dict[str, Any]],
    best: dict[tuple[str, str, str], dict[str, Any]],
    runs: dict[tuple[str, str, str], list[dict[str, Any]]],
    documents: dict[tuple[str, str], dict[str, Any]],
    references: dict[str, int],
    args: argparse.Namespace,
) -> None:
    strict_keys = strict_fit_keys(keys, best)
    soft_costs = best_soft_costs(keys, best)
    proven_keys = {
        key
        for key in keys
        if any(
            proof_present(group)
            and any(is_fit(record) and record.get("reuse_cost") == references.get(key) for record in group)
            for (candidate_policy, candidate_key, _), group in runs.items()
            if candidate_policy == "soft_v1" and candidate_key == key
        )
    }
    source_rows = []
    for source in ("PyPTO", "PyPTO-Lib"):
        source_keys = [key for key in keys if namespace(documents[("soft_v1", key)]) == source]
        source_rows.append(
            (
                source,
                len(source_keys),
                sum(
                    int(documents[("soft_v1", key)]["metadata"]["dsa_rp_cross_pipe_edges"])
                    for key in source_keys
                ),
                len(set(source_keys) & strict_keys),
                sum(soft_costs.get(key) == 0 for key in source_keys),
                sum(references.get(key) == 0 for key in source_keys),
                len(set(source_keys) & proven_keys),
                sum(references[key] for key in source_keys if key in references),
            )
        )

    with path.open("w", encoding="utf-8") as output:
        output.write(
            "# DSA with reuse penalties\n\n"
            f"The corpus contains {len(keys)} matched hard/soft problems. The soft problem fits "
            "within the architecture capacity and minimizes activated unit-weight cross-pipe "
            "reuse edges. The hard counterpart forbids all of the same overlaps. All reported "
            "placements were independently validated by `dsa-suite`. Pre-existing non-cross-pipe "
            "penalties are unchanged in both variants, so a fitting hard placement contributes "
            "its remaining penalty as a valid upper bound for the soft problem.\n\n"
            "Full per-run objectives and diagnostics are in `results.jsonl`; per-algorithm "
            "best objectives are in `summary.csv`; `paired-objectives.csv` records the compact "
            "per-instance comparison.\n\n"
            f"Configuration: seeds `{args.seeds}`, search budget `{args.iterations}`, restarts "
            f"`{args.restarts}`, and stagnation limit `{args.stagnation}`. Runtime is "
            "machine-dependent.\n\n"
            "Regenerate from the repository root:\n\n"
            "```bash\n"
            "./build/dsa-suite \\\n"
            "  --pypto benchmarks/pypto \\\n"
            "  --pypto benchmarks/pypto-lib \\\n"
            "  --dsa-rp-variants-only \\\n"
            f"  --output-dir {args.output_dir} \\\n"
            "  --run-label dsa-rp-v1 \\\n"
            f"  --seeds {args.seeds} \\\n"
            f"  --iterations {args.iterations} \\\n"
            f"  --restarts {args.restarts} \\\n"
            f"  --stagnation {args.stagnation} \\\n"
            "  --no-minimalloc \\\n"
            "  --no-core-relaxations\n"
            f"python tools/report_dsa_rp_results.py {args.output_dir}/results.jsonl \\\n"
            f"  --output-dir {args.output_dir}\n"
            "```\n\n"
            "## Corpus outcome\n\n"
            "| Source | Pairs | Edges | Strict fit found | Soft solver found 0 | "
            "Best-known cost 0 | Optimum proven | Sum best cost |\n"
            "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n"
        )
        for row in source_rows:
            output.write("| " + " | ".join(str(value) for value in row) + " |\n")
        unresolved = [key for key in keys if key not in references]
        if unresolved:
            names = ", ".join(
                f"`{documents[('soft_v1', key)]['metadata'].get('dsa_rp_source_instance', key)}`"
                for key in unresolved
            )
            output.write(
                f"\nNo tested method found a capacity-fitting placement for "
                f"{len(unresolved)} pair(s): {names}. They remain in every denominator.\n"
            )

        output.write(
            "\n## Soft-penalty algorithm comparison\n\n"
            "`Fits` counts capacity-feasible validated placements. `Best` counts ties with the "
            "lowest validated reuse cost found across both counterparts. `Proven` counts solver "
            "certificates, not merely best-known values. `Mean active` averages "
            "`reuse_cost / recognized_edges` per fitted instance. Runtime is a geometric mean "
            "of per-instance median ratios to first fit; its parenthesized value is the number "
            "of positive-resolution comparisons.\n\n"
            "| Class | Algorithm | Applicable | Fits | Best | Zero | Proven | Sum cost | "
            "Mean active | Runtime / FF |\n"
            "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n"
        )
        for row in algorithm_stats:
            output.write(
                f"| {row['category']} | `{row['method']}` | {row['applicable']}/{len(keys)} | "
                f"{row['fits']}/{len(keys)} | {row['wins']}/{len(keys)} | "
                f"{row['zero']}/{len(keys)} | {row['proofs']}/{len(keys)} | "
                f"{row['total_cost']} | {percent(row['mean_active'])} | "
                f"{ratio(row['runtime_ratio'], row['runtime_instances'])} |\n"
            )

        stats_by_method = {row["method"]: row for row in algorithm_stats}
        cypress = stats_by_method["cypress_relaxation"]
        canonical = stats_by_method["canonical_greedy"]
        excess_cost = 100.0 * ((cypress["total_cost"] - canonical["total_cost"]) / canonical["total_cost"])
        output.write(
            "\n**Cypress comparison.** On this corpus, Cypress relaxation produces substantially "
            "worse objectives than canonical greedy: "
            f"its aggregate penalty is {cypress['total_cost']} versus "
            f"{canonical['total_cost']} ({excess_cost:.1f}% higher), it reaches the best-known "
            f"objective on {cypress['wins']} versus {canonical['wins']} instances, and it finds "
            f"{cypress['zero']} versus {canonical['zero']} zero-penalty placements. The runtime "
            f"ratios are {cypress['runtime_ratio']:.2f}x and "
            f"{canonical['runtime_ratio']:.2f}x first fit, respectively.\n"
        )

        output.write(
            "\n## Hard-counterpart search\n\n"
            "These rows ask only whether the same relations can all remain hard while fitting "
            "capacity. Failure is a bounded-search result unless an algorithm supplies an "
            "infeasibility certificate.\n\n"
            "| Algorithm | Applicable | Fits | Runtime / FF |\n"
            "| --- | ---: | ---: | ---: |\n"
        )
        for row in strict_stats:
            output.write(
                f"| `{row['method']}` | {row['applicable']}/{len(keys)} | "
                f"{row['fits']}/{len(keys)} | "
                f"{ratio(row['runtime_ratio'], row['runtime_instances'])} |\n"
            )


def main() -> None:
    args = parse_args()
    records = load_records(args.results_jsonl)
    best, runs, documents = summarize_runs(records)
    keys = validate_pairs(documents)
    references = best_soft_costs(keys, best)
    for key, cost in best_hard_costs(keys, best).items():
        references[key] = min(references.get(key, cost), cost)
    algorithm_stats = algorithm_rows(keys, best, runs, documents, references)
    strict_stats = hard_rows(keys, best, runs)

    args.output_dir.mkdir(parents=True, exist_ok=True)
    write_algorithm_csv(args.output_dir / "algorithm-comparison.csv", algorithm_stats)
    write_paired_csv(
        args.output_dir / "paired-objectives.csv",
        keys,
        best,
        runs,
        documents,
        references,
    )
    write_report(
        args.output_dir / "report.md",
        keys,
        algorithm_stats,
        strict_stats,
        best,
        runs,
        documents,
        references,
        args,
    )


if __name__ == "__main__":
    main()
