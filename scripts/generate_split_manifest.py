#!/usr/bin/env python3
"""Generate small CSV manifests for the split benchmark workflow."""

import argparse
import csv
from pathlib import Path


RATIOS = ["0.5", "0.1", "0.01", "0.001"]
BIHS_K_MODES = ["k1", "optk", "rootk"]
FIXED_RUBIK_ALGORITHMS = ["bihs_bloom", "idths_trans", "ida"]


def write_rows(path: Path, rows: list[dict[str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fields = ["job_id", "domain", "instance", "phase", "algorithm", "ratio", "k_mode"]
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, lineterminator="\n")
        writer.writeheader()
        for job_id, row in enumerate(rows):
            row = dict(row)
            row["job_id"] = str(job_id)
            writer.writerow(row)


def calibration_rows(domain: str, instance_start: int, instance_end: int) -> list[dict[str, str]]:
    rows = []
    for instance in range(instance_start, instance_end):
        for algorithm in ["astar", "rev_astar", "mm"]:
            rows.append({
                "domain": domain,
                "instance": str(instance),
                "phase": "calibrate",
                "algorithm": algorithm,
                "ratio": "",
                "k_mode": "",
            })
    return rows


def run_rows(domain: str, params_path: Path) -> list[dict[str, str]]:
    rows = []
    with params_path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            if row["domain"] != domain or row["status"] != "ok":
                continue
            for ratio in RATIOS:
                for k_mode in BIHS_K_MODES:
                    rows.append({
                        "domain": domain,
                        "instance": row["instance"],
                        "phase": "run",
                        "algorithm": "bihs_bloom",
                        "ratio": ratio,
                        "k_mode": k_mode,
                    })
                rows.append({
                    "domain": domain,
                    "instance": row["instance"],
                    "phase": "run",
                    "algorithm": "idths_trans",
                    "ratio": ratio,
                    "k_mode": "",
                })
    return rows


def rubik_fixed_rows(instance_start: int, instance_end: int) -> list[dict[str, str]]:
    rows = []
    for instance in range(instance_start, instance_end):
        for algorithm in FIXED_RUBIK_ALGORITHMS:
            rows.append({
                "domain": "rubik",
                "instance": str(instance),
                "phase": "fixed-run",
                "algorithm": algorithm,
                "ratio": "1.0",
                "k_mode": "k1" if algorithm == "bihs_bloom" else "",
            })
    return rows


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--phase", choices=["calibrate", "run", "rubik-fixed"], required=True)
    parser.add_argument("--domain", choices=["stp", "rubik"], default="stp")
    parser.add_argument("--instance-start", type=int, default=0)
    parser.add_argument("--instance-end", type=int, default=1)
    parser.add_argument("--params", type=Path, default=Path("results/split/params/stp_params.csv"))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    if args.phase == "calibrate":
        rows = calibration_rows(args.domain, args.instance_start, args.instance_end)
    elif args.phase == "rubik-fixed":
        if args.domain != "rubik":
            raise SystemExit("--phase rubik-fixed requires --domain rubik")
        rows = rubik_fixed_rows(args.instance_start, args.instance_end)
    else:
        rows = run_rows(args.domain, args.params)

    write_rows(args.output, rows)
    print(f"Wrote {len(rows)} {args.phase} manifest rows to {args.output}")


if __name__ == "__main__":
    main()
