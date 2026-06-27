#!/usr/bin/env python3
"""Merge per-instance STP and Pancake CSV shards."""

import argparse
import csv
from pathlib import Path


OUTPUTS = {
    "stp": ("benchmark_stp_korf100.csv", "bloom_convergence.csv"),
    "pancake": ("benchmark_pancake20_100.csv", "bloom_convergence_pancake20.csv"),
}


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
        raise ValueError(f"No non-empty benchmark files found for {output.name}")

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
    rows_by_key: dict[tuple[str, ...], list[str]] = {}
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
        raise ValueError(f"No non-empty convergence files found for {output.name}")

    def sort_key(row: list[str]) -> tuple:
        return (int(row[0]), float(row[2]), int(row[3]), int(row[4]), tuple(row))

    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(header)
        writer.writerows(sorted(rows_by_key.values(), key=sort_key))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input-dir", type=Path, default=Path("results/stp_pancake/parts"))
    parser.add_argument("--output-dir", type=Path, default=Path("results/merged"))
    args = parser.parse_args()

    for domain, (benchmark_name, convergence_name) in OUTPUTS.items():
        benchmark_files = sorted(args.input_dir.glob(f"benchmark_{domain}_*.csv"))
        convergence_files = sorted(args.input_dir.glob(f"convergence_{domain}_*.csv"))
        if not benchmark_files or not convergence_files:
            raise SystemExit(f"No {domain} CSV shards found in {args.input_dir}")
        merge_benchmarks(benchmark_files, args.output_dir / benchmark_name)
        merge_convergence(convergence_files, args.output_dir / convergence_name)
        print(f"Merged {len(benchmark_files)} {domain} benchmark shards and {len(convergence_files)} convergence shards")


if __name__ == "__main__":
    main()
