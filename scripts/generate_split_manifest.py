#!/usr/bin/env python3
"""Generate small CSV manifests for the split STP benchmark workflow."""

import argparse
import csv
from pathlib import Path


RATIOS = ["0.5", "0.1", "0.01", "0.001"]


def write_rows(path: Path, rows: list[dict[str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fields = ["job_id", "domain", "instance", "phase", "algorithm", "ratio"]
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, lineterminator="\n")
        writer.writeheader()
        for job_id, row in enumerate(rows):
            row = dict(row)
            row["job_id"] = str(job_id)
            writer.writerow(row)


def calibration_rows(instance_start: int, instance_end: int) -> list[dict[str, str]]:
    rows = []
    for instance in range(instance_start, instance_end):
        for algorithm in ["astar", "rev_astar", "mm"]:
            rows.append({
                "domain": "stp",
                "instance": str(instance),
                "phase": "calibrate",
                "algorithm": algorithm,
                "ratio": "",
            })
    return rows


def run_rows(params_path: Path) -> list[dict[str, str]]:
    rows = []
    with params_path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            if row["domain"] != "stp" or row["status"] != "ok":
                continue
            for ratio in RATIOS:
                for algorithm in ["bihs_bloom", "idths_trans"]:
                    rows.append({
                        "domain": "stp",
                        "instance": row["instance"],
                        "phase": "run",
                        "algorithm": algorithm,
                        "ratio": ratio,
                    })
    return rows


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--phase", choices=["calibrate", "run"], required=True)
    parser.add_argument("--instance-start", type=int, default=0)
    parser.add_argument("--instance-end", type=int, default=1)
    parser.add_argument("--params", type=Path, default=Path("results/split/params/stp_params.csv"))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    if args.phase == "calibrate":
        rows = calibration_rows(args.instance_start, args.instance_end)
    else:
        rows = run_rows(args.params)

    write_rows(args.output, rows)
    print(f"Wrote {len(rows)} {args.phase} manifest rows to {args.output}")


if __name__ == "__main__":
    main()
