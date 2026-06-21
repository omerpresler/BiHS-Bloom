#!/usr/bin/env python3
"""Merge per-array-task STP CSV files into the files used by report scripts."""

import argparse
import csv
from pathlib import Path


def merge_benchmarks(files: list[Path], output: Path) -> None:
    header = None
    result_rows: dict[int, list[str]] = {}
    param_rows: dict[tuple[int, str, str, str], list[str]] = {}

    for path in files:
        with path.open(newline="", encoding="utf-8") as handle:
            rows = csv.reader(handle)
            current_header = next(rows, None)
            if not current_header:
                continue
            if header is None:
                header = current_header
            elif current_header != header:
                raise ValueError(f"Benchmark header mismatch in {path}")

            for row in rows:
                if not row:
                    continue
                if row[0] == "BIHS_PARAM":
                    key = (int(row[1]), row[2], row[9] if len(row) > 9 else "", row[10] if len(row) > 10 else "")
                    param_rows[key] = row
                else:
                    result_rows[int(row[0])] = row

    if header is None:
        raise ValueError("No non-empty benchmark files found")

    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(header)
        for key in sorted(param_rows):
            writer.writerow(param_rows[key])
        for instance in sorted(result_rows):
            writer.writerow(result_rows[instance])


def merge_convergence(files: list[Path], output: Path) -> None:
    header = None
    rows_by_key: dict[tuple, list[str]] = {}
    for path in files:
        with path.open(newline="", encoding="utf-8") as handle:
            reader = csv.reader(handle)
            current_header = next(reader, None)
            if not current_header:
                continue
            if header is None:
                header = current_header
            elif current_header != header:
                raise ValueError(f"Convergence header mismatch in {path}")
            for row in reader:
                if row:
                    rows_by_key[tuple(row)] = row

    if header is None:
        raise ValueError("No non-empty convergence files found")

    def sort_key(row: list[str]) -> tuple:
        return (int(row[0]), float(row[2]), int(row[3]), int(row[4]), tuple(row))

    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(header)
        writer.writerows(sorted(rows_by_key.values(), key=sort_key))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input-dir", type=Path, default=Path("results/parts"))
    parser.add_argument("--output-dir", type=Path, default=Path("results/merged"))
    args = parser.parse_args()

    benchmarks = sorted(args.input_dir.glob("benchmark_*.csv"))
    convergence = sorted(args.input_dir.glob("convergence_*.csv"))
    if not benchmarks or not convergence:
        parser.error(f"No batch CSV files found in {args.input_dir}")

    merge_benchmarks(benchmarks, args.output_dir / "benchmark_stp_korf100.csv")
    merge_convergence(convergence, args.output_dir / "bloom_convergence.csv")
    print(f"Merged {len(benchmarks)} benchmark and {len(convergence)} convergence files into {args.output_dir}")


if __name__ == "__main__":
    main()
