"""Run one calibrated arm of the 15-puzzle exact-batching pilot."""

import argparse
import csv
import hashlib
import json
from pathlib import Path
import shlex
import socket
import subprocess


ROOT = Path("results/stp_exact_batches_20261120")
HALF_BLOOM_ROOT = Path("results/stp_half_bloom_20261120")
REFERENCE = Path("results/fresh_bytes_20261016/manifests/stp.jsonl")
REFERENCE_SHA = "558d41905717de089057724eb62955bdeefe83c5d4742a100c7927e35f2bf939"
INSTANCES = (48, 55)
ARMS = ("bihs_bloom_aem_control", "bihs_bloom_aem")
DEADLINE_SECONDS = 1800


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def calibration(instance):
    if sha256(REFERENCE) != REFERENCE_SHA:
        raise ValueError("Sealed STP calibration reference differs from the recorded digest")
    with REFERENCE.open() as source:
        matches = [
            (line_number, row)
            for line_number, line in enumerate(source, 1)
            if (row := json.loads(line))["instance"] == instance
            and row["domain"] == "stp" and row["heuristic"] == "manhattan"
            and row["ratio"] == "0.01" and row["algorithm"] == "bihs_bloom"
        ]
    (line_number, record), = matches
    cal = record["calibration"]
    references = [int(cal[f"{algorithm}_states"]) for algorithm in ("astar", "rev_astar", "mm")]
    if (cal["status"] != "ok" or cal["reason"] != "ok"
            or cal["domain"] != "stp" or cal["heuristic"] != "manhattan"
            or int(cal["instance"]) != instance or int(cal["seed"]) != instance
            or int(cal["state_bits"]) != 45 or min(references) <= 0
            or int(cal["best_um_states"]) != min(references) or int(cal["solution_length"]) <= 0):
        raise ValueError(f"Invalid matching calibration for Korf #{instance}")
    return line_number, cal


def run(task, half_bloom=False):
    if not 0 <= task < len(INSTANCES) * len(ARMS):
        raise ValueError("Task outside the four-run pilot")
    instance = INSTANCES[task // len(ARMS)]
    arm = ARMS[task % len(ARMS)]
    source_line, cal = calibration(instance)
    memory_bytes = int(cal["best_um_states"]) * int(cal["state_bits"]) // 800
    root = HALF_BLOOM_ROOT if half_bloom else ROOT
    binary = root / "bin/comparison"
    out = root / "runs" / str(task)
    out.mkdir(parents=True, exist_ok=False)
    summary = out / "summary.csv"
    events = out / "events.csv"
    command = [
        str(binary), "--domain", "stp", "--instance", str(instance),
        "--algorithm", arm, "--heuristic", "manhattan", "--f2f", "na",
        "--consistency", "1", "--memory-bytes", str(memory_bytes),
        "--k-hashes", "1", "--bloom-seed", "20260824",
        "--bihs-timeout-seconds", str(DEADLINE_SECONDS),
        "--aem-exact-batches", "--aem-alpha", "1", "--aem-output", str(events),
        "--summary-output", str(summary), "--level-output", str(out / "levels.csv"),
        "--scan-output", str(out / "scans.csv"),
    ]
    if half_bloom:
        command.append("--aem-half-bloom")
    metadata = {
        "task": task, "instance": instance, "algorithm": arm, "budget_fraction": "1/100",
        "bloom_layout": "half_budget" if half_bloom else "quarter_budget",
        "memory_budget_bytes": memory_bytes, "rounding": "floor(best_um_states * state_bits / 800)",
        "logical_state_capacity": memory_bytes * 8 // int(cal["state_bits"]),
        "calibration": cal, "calibration_reference": str(REFERENCE), "calibration_line": source_line,
        "calibration_sha256": REFERENCE_SHA, "calibration_excluded": True,
        "binary_sha256": sha256(binary), "hostname": socket.gethostname(), "command": command,
    }
    (out / "provenance.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(shlex.join(command), flush=True)
    subprocess.run(command, check=True)
    with summary.open(newline="") as source:
        row, = csv.DictReader(source)
    if (None in row or None in row.values() or row["domain"] != "stp"
            or int(row["instance"]) != instance or row["algorithm"] != arm
            or row["heuristic"] != "manhattan" or int(row["memory_budget_bytes"]) != memory_bytes
            or row["status"] not in ("ok", "timeout")
            or (row["status"] == "ok" and int(row["solution_length"]) != int(cal["solution_length"]))
            or (row["status"] == "timeout" and int(row["solution_length"]) != -1)
            or not 0 < int(row["aem_peak_managed_bytes"]) <= memory_bytes
            or int(row["aem_overflows"]) != 0):
        raise ValueError(f"Invalid summary for task {task}")
    if half_bloom and (int(row["aem_seed_rebuilds"]) < 0
                       or not memory_bytes // 4 < int(row["aem_bloom_buffer_bytes"]) <= memory_bytes // 2):
        raise ValueError(f"Invalid half-budget filter layout for task {task}")
    attempts = occupancy_only = 0
    min_capacity = max_capacity = None
    with events.open(newline="") as source:
        for event in csv.DictReader(source):
            capacity = int(event["exact_capacity"])
            if (None in event or None in event.values()
                    or int(event["managed_budget_bytes"]) != memory_bytes
                    or not 0 <= int(event["exact_states"]) <= capacity
                    or event["outcome"] not in ("solved", "no_intersection", "empty", "timeout")):
                raise ValueError(f"Invalid materialization event for task {task}")
            attempts += 1
            occupancy_only += int(event["insertion_attempts"]) > capacity
            min_capacity = capacity if min_capacity is None else min(min_capacity, capacity)
            max_capacity = capacity if max_capacity is None else max(max_capacity, capacity)
    if attempts != int(row["aem_attempts"]) or (arm == ARMS[0] and occupancy_only):
        raise ValueError(f"Inconsistent materialization trigger counts for task {task}")
    metadata.update(summary=row, occupancy_only_attempts=occupancy_only,
                    exact_capacity_min=min_capacity, exact_capacity_max=max_capacity)
    (out / "validated.json").write_text(json.dumps(metadata, indent=2) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("task", type=int)
    parser.add_argument("--half-bloom", action="store_true")
    args = parser.parse_args()
    run(args.task, args.half_bloom)
