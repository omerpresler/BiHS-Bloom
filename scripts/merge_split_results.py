#!/usr/bin/env python3
"""Merge split benchmark CSVs into params and legacy-compatible outputs."""

import argparse
import csv
from pathlib import Path


RATIOS = [
    ("0.5", "50pct"),
    ("0.1", "10pct"),
    ("0.01", "1pct"),
    ("0.001", "0_1pct"),
]
BIHS_K_MODES = ["k1", "optk", "rootk"]
BIHS_SPLIT_MODE = "idths_workload"

BASE_HEADER = [
    "instance", "solution_length",
    "a_star_time", "rev_a_star_time", "bae_time", "nbs_time", "mm_time",
    "ida_time", "parallel_ida_time", "rev_ida_time",
    "a_star_nodes", "rev_a_star_nodes", "bae_nodes", "nbs_nodes", "mm_nodes",
    "ida_nodes", "parallel_ida_nodes", "rev_ida_nodes",
]


def read_csv_files(paths: list[Path]) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    for path in paths:
        with path.open(newline="", encoding="utf-8") as handle:
            rows.extend(csv.DictReader(handle))
    return rows


def merge_params(domain: str, input_dir: Path, output: Path) -> None:
    rows = read_csv_files(sorted(input_dir.glob("calibration_*.csv")))
    rows = [row for row in rows if row["domain"] == domain]
    by_instance: dict[int, dict[str, dict[str, str]]] = {}
    for row in rows:
        by_instance.setdefault(int(row["instance"]), {})[row["algorithm"]] = row

    output.parent.mkdir(parents=True, exist_ok=True)
    fields = [
        "domain", "instance", "status", "solution_length", "min_memory_items",
        "frontier_items", "max_baseline_time", "reason",
        "a_star_time", "rev_a_star_time", "mm_time",
        "a_star_nodes", "rev_a_star_nodes", "mm_nodes",
    ]
    with output.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, lineterminator="\n")
        writer.writeheader()
        for instance in sorted(by_instance):
            algs = by_instance[instance]
            missing = [name for name in ["astar", "rev_astar", "mm"] if name not in algs]
            reason = ""
            status = "ok"
            if missing:
                status = "missing_params"
                reason = "missing_" + "_".join(missing)
            elif any(algs[name]["status"] != "ok" for name in ["astar", "rev_astar", "mm"]):
                status = "missing_params"
                reason = "calibration_failed"
            elif int(algs["mm"]["frontier_items"]) <= 0:
                status = "missing_params"
                reason = "missing_mm_frontier"

            if status == "ok":
                memory_items = [
                    int(algs["astar"]["memory_items"]),
                    int(algs["rev_astar"]["memory_items"]),
                    int(algs["mm"]["memory_items"]),
                ]
                solution_length = algs["astar"]["solution_length"]
                min_memory = str(min(memory_items))
                frontier = algs["mm"]["frontier_items"]
                max_time = str(max(float(algs[name]["time"]) for name in ["astar", "rev_astar", "mm"]))
            else:
                solution_length = "-1"
                min_memory = "0"
                frontier = "0"
                max_time = "-1"

            writer.writerow({
                "domain": domain,
                "instance": str(instance),
                "status": status,
                "solution_length": solution_length,
                "min_memory_items": min_memory,
                "frontier_items": frontier,
                "max_baseline_time": max_time,
                "reason": reason or "ok",
                "a_star_time": algs.get("astar", {}).get("time", "-3"),
                "rev_a_star_time": algs.get("rev_astar", {}).get("time", "-3"),
                "mm_time": algs.get("mm", {}).get("time", "-3"),
                "a_star_nodes": algs.get("astar", {}).get("nodes", "0"),
                "rev_a_star_nodes": algs.get("rev_astar", {}).get("nodes", "0"),
                "mm_nodes": algs.get("mm", {}).get("nodes", "0"),
            })
    print(f"Wrote params to {output}")


def benchmark_header() -> list[str]:
    header = list(BASE_HEADER)
    header.extend([
        f"bihs_bloom_time_{slug}_{k_mode}_{BIHS_SPLIT_MODE}"
        for _, slug in RATIOS
        for k_mode in BIHS_K_MODES
    ])
    header.extend([
        f"bihs_bloom_nodes_{slug}_{k_mode}_{BIHS_SPLIT_MODE}"
        for _, slug in RATIOS
        for k_mode in BIHS_K_MODES
    ])
    header.extend([f"idths_trans_time_{slug}" for _, slug in RATIOS])
    header.extend([f"idths_trans_nodes_{slug}" for _, slug in RATIOS])
    header.extend([f"idths_trans_necessary_nodes_{slug}" for _, slug in RATIOS])
    header.extend([f"idths_trans_storage_states_{slug}" for _, slug in RATIOS])
    for metric in ["bound_cycles", "forward_scans", "backward_scans", "type_split_scans", "extraction_scans"]:
        header.extend([
            f"bihs_{metric}_{slug}_{k_mode}_{BIHS_SPLIT_MODE}"
            for _, slug in RATIOS
            for k_mode in BIHS_K_MODES
        ])
    for metric in ["bound_cycles", "forward_scans", "backward_scans",
                   "table_full_backward_scans", "end_cycle_backward_scans"]:
        header.extend([f"idths_{metric}_{slug}" for _, slug in RATIOS])
    return header


def merge_convergence(domain: str, input_dir: Path, output: Path) -> None:
    files = sorted(input_dir.glob(f"convergence_{domain}_*.csv"))
    header = [
        "instance", "size_kib", "ratio", "total_depth", "iteration", "n_inserted",
        "n_unique", "estimated_fp", "bits_set", "fill_ratio", "expected_fill_ratio",
        "materialized_forward", "materialized_backward", "materialized_total", "phase",
        "type_index", "type_count", "bound_cycle", "forward_depth", "backward_depth",
        "cumulative_forward_scans", "cumulative_backward_scans", "cumulative_type_scans",
        "first_forward_work", "first_backward_work", "observed_next_f",
        "k_mode", "k_hashes", "split_mode",
    ]
    rows = []
    for path in files:
        with path.open(newline="", encoding="utf-8") as handle:
            reader = csv.reader(handle)
            current_header = next(reader, None)
            if not current_header:
                continue
            for row in reader:
                if row:
                    rows.append(row)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(header)
        writer.writerows(rows)


def merge_final(domain: str, params_path: Path, run_dir: Path, convergence_dir: Path, output_dir: Path) -> None:
    params_rows = read_csv_files([params_path])
    run_rows = read_csv_files(sorted(run_dir.glob("result_*.csv")))
    run_rows = [row for row in run_rows if row["domain"] == domain]
    run_by_key = {
        (row["instance"], row["algorithm"], row["ratio"], row.get("k_mode", "")): row
        for row in run_rows
    }

    output_dir.mkdir(parents=True, exist_ok=True)
    benchmark_name = "benchmark_stp_korf100.csv"
    convergence_name = "bloom_convergence.csv"
    if domain == "rubik":
        benchmark_name = "benchmark_rubik_korf_10.csv"
        convergence_name = "bloom_convergence_rubik_korf.csv"
    benchmark_path = output_dir / benchmark_name
    header = benchmark_header()
    param_rows = []
    result_rows = []

    for params in params_rows:
        if params["domain"] != domain:
            continue
        instance = params["instance"]
        if params["status"] != "ok":
            continue

        row = {name: "-3" for name in header}
        row.update({
            "instance": instance,
            "solution_length": params["solution_length"],
            "a_star_time": params["a_star_time"],
            "rev_a_star_time": params["rev_a_star_time"],
            "mm_time": params["mm_time"],
            "a_star_nodes": params["a_star_nodes"],
            "rev_a_star_nodes": params["rev_a_star_nodes"],
            "mm_nodes": params["mm_nodes"],
            "bae_nodes": "0",
            "nbs_nodes": "0",
            "ida_nodes": "0",
            "parallel_ida_nodes": "0",
            "rev_ida_nodes": "0",
        })

        for ratio, slug in RATIOS:
            for k_mode in BIHS_K_MODES:
                bihs = run_by_key.get((instance, "bihs_bloom", ratio, k_mode))
                if bihs:
                    split_mode = bihs.get("split_mode") or BIHS_SPLIT_MODE
                    row[f"bihs_bloom_time_{slug}_{k_mode}_{split_mode}"] = bihs["time"]
                    row[f"bihs_bloom_nodes_{slug}_{k_mode}_{split_mode}"] = bihs["nodes"]
                    row[f"bihs_bound_cycles_{slug}_{k_mode}_{split_mode}"] = bihs.get("bound_cycles", "0")
                    row[f"bihs_forward_scans_{slug}_{k_mode}_{split_mode}"] = bihs.get("forward_scans", "0")
                    row[f"bihs_backward_scans_{slug}_{k_mode}_{split_mode}"] = bihs.get("backward_scans", "0")
                    row[f"bihs_type_split_scans_{slug}_{k_mode}_{split_mode}"] = bihs.get("type_split_scans", "0")
                    extraction_scans = int(bihs.get("materialization_scans", "0")) + int(bihs.get("intersection_scans", "0"))
                    row[f"bihs_extraction_scans_{slug}_{k_mode}_{split_mode}"] = str(extraction_scans)
                    if row["solution_length"] == "-1" and bihs["status"] == "ok":
                        row["solution_length"] = bihs["solution_length"]
                    converged = "1" if bihs["status"] == "ok" else "0"
                    param_rows.append([
                        "BIHS_PARAM", instance, ratio, bihs["size_kib"], bihs["k_hashes"],
                        bihs["time"], bihs["nodes"], bihs["fp_est"], converged,
                        k_mode, split_mode,
                    ])
            idths = run_by_key.get((instance, "idths_trans", ratio, ""))
            if idths:
                row[f"idths_trans_time_{slug}"] = idths["time"]
                row[f"idths_trans_nodes_{slug}"] = idths["nodes"]
                row[f"idths_trans_necessary_nodes_{slug}"] = idths["necessary_nodes"]
                row[f"idths_trans_storage_states_{slug}"] = idths["storage_states"]
                row[f"idths_bound_cycles_{slug}"] = idths.get("bound_cycles", "0")
                row[f"idths_forward_scans_{slug}"] = idths.get("forward_scans", "0")
                row[f"idths_backward_scans_{slug}"] = idths.get("backward_scans", "0")
                row[f"idths_table_full_backward_scans_{slug}"] = idths.get("table_full_backward_scans", "0")
                row[f"idths_end_cycle_backward_scans_{slug}"] = idths.get("end_cycle_backward_scans", "0")
                if row["solution_length"] == "-1" and idths["status"] == "ok":
                    row["solution_length"] = idths["solution_length"]

        result_rows.append(row)

    with benchmark_path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(header)
        writer.writerows(param_rows)
        for row in sorted(result_rows, key=lambda item: int(item["instance"])):
            writer.writerow([row[name] for name in header])

    merge_convergence(domain, convergence_dir, output_dir / convergence_name)
    print(f"Wrote merged benchmark and convergence files to {output_dir}")


def merge_rubik_fixed(run_dir: Path, convergence_dir: Path, output_dir: Path) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    result_files = sorted(run_dir.glob("result_rubik_fixed_*.csv"))
    rows = read_csv_files(result_files)
    rows = [row for row in rows if row.get("domain") == "rubik"]

    fields = [
        "domain", "instance", "algorithm", "ratio", "status", "time", "nodes",
        "necessary_nodes", "storage_states", "size_kib", "k_hashes", "fp_est",
        "solution_length", "k_mode", "split_mode", "scan_schema", "bound_cycles",
        "forward_scans", "backward_scans", "table_full_backward_scans",
        "end_cycle_backward_scans", "type_split_scans", "materialization_scans",
        "intersection_scans", "frontier_scans", "total_scans",
    ]
    with (output_dir / "benchmark_rubik_korf_fixed128g_k1.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, lineterminator="\n")
        writer.writeheader()
        for row in sorted(rows, key=lambda item: (int(item["instance"]), item["algorithm"])):
            writer.writerow({field: row.get(field, "") for field in fields})

    header = [
        "instance", "size_kib", "ratio", "total_depth", "iteration", "n_inserted",
        "n_unique", "estimated_fp", "bits_set", "fill_ratio", "expected_fill_ratio",
        "materialized_forward", "materialized_backward", "materialized_total", "phase",
        "type_index", "type_count", "bound_cycle", "forward_depth", "backward_depth",
        "cumulative_forward_scans", "cumulative_backward_scans", "cumulative_type_scans",
        "first_forward_work", "first_backward_work", "observed_next_f",
        "k_mode", "k_hashes", "split_mode",
    ]
    convergence_rows = []
    for path in sorted(convergence_dir.glob("convergence_rubik_fixed_*.csv")):
        with path.open(newline="", encoding="utf-8") as handle:
            reader = csv.reader(handle)
            next(reader, None)
            convergence_rows.extend(row for row in reader if row)
    with (output_dir / "bloom_convergence_rubik_korf_fixed128g_k1.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(header)
        writer.writerows(convergence_rows)

    print(f"Wrote fixed Rubik benchmark and convergence files to {output_dir}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", choices=["params", "final", "rubik-fixed"], required=True)
    parser.add_argument("--domain", choices=["stp", "rubik"], default="stp")
    parser.add_argument("--calibration-dir", type=Path, default=Path("results/split/calibration_parts"))
    parser.add_argument("--params", type=Path, default=Path("results/split/params/stp_params.csv"))
    parser.add_argument("--run-dir", type=Path, default=Path("results/split/run_parts"))
    parser.add_argument("--convergence-dir", type=Path, default=Path("results/split/convergence_parts"))
    parser.add_argument("--output-dir", type=Path, default=Path("results/merged"))
    args = parser.parse_args()

    if args.mode == "params":
        merge_params(args.domain, args.calibration_dir, args.params)
    elif args.mode == "final":
        merge_final(args.domain, args.params, args.run_dir, args.convergence_dir, args.output_dir)
    else:
        merge_rubik_fixed(args.run_dir, args.convergence_dir, args.output_dir)


if __name__ == "__main__":
    main()
