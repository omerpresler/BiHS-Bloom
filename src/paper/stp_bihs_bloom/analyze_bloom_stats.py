#!/usr/bin/env python3
import argparse
import csv
from collections import Counter, defaultdict
from statistics import mean


def parse_inserted_list(value: str):
    text = value.strip()
    if text.startswith("[") and text.endswith("]"):
        text = text[1:-1]
    items = []
    for part in text.split(","):
        part = part.strip()
        if not part:
            continue
        try:
            items.append(int(part))
        except ValueError:
            continue
    return items


def load_rows(path: str):
    rows = []
    with open(path, newline="") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            row["Puzzle"] = int(row["Puzzle"])
            row["Size_KiB"] = int(row["Size_KiB"])
            row["K_Hashes"] = int(row["K_Hashes"])
            row["Set_Ratio"] = float(row["Set_Ratio"])
            row["Set_Limit"] = int(row["Set_Limit"])
            row["Set_Size"] = int(row["Set_Size"])
            row["Loop_Count"] = int(row["Loop_Count"])
            row["Final_Inserted"] = int(row["Final_Inserted"])
            row["Inserted_List"] = parse_inserted_list(row["Inserted"])
            rows.append(row)
    return rows


def summarize(rows):
    print("=== Overall ===")
    print(f"Total runs: {len(rows)}")
    term_counts = Counter(r["Termination"] for r in rows)
    for term, count in term_counts.items():
        print(f"  {term}: {count}")

    print("\n=== By mode/ratio ===")
    grouped = defaultdict(list)
    for row in rows:
        key = (row["Mode"], row["Set_Ratio"])
        grouped[key].append(row)

    for (mode, ratio), items in sorted(grouped.items()):
        avg_loop = mean(r["Loop_Count"] for r in items)
        avg_final = mean(r["Final_Inserted"] for r in items)
        term_counts = Counter(r["Termination"] for r in items)
        terms = ", ".join(f"{k}={v}" for k, v in term_counts.items())
        print(
            f"  {mode} ratio={ratio:.2f} "
            f"runs={len(items)} avg_loops={avg_loop:.1f} "
            f"avg_final={avg_final:.1f} terms({terms})"
        )


def report_non_converged(rows, top_n: int):
    non_converged = [r for r in rows if r["Termination"] != "stabilized"]
    print("\n=== Non-converged (Termination != stabilized) ===")
    print(f"Total: {len(non_converged)}")
    if not non_converged:
        return

    non_converged.sort(key=lambda r: (-r["Loop_Count"], -r["Final_Inserted"]))
    print(f"\nTop {min(top_n, len(non_converged))} by Loop_Count:")
    for row in non_converged[:top_n]:
        print(
            "  puzzle={Puzzle} size={Size_KiB}kib k={K_Hashes} "
            "mode={Mode} ratio={Set_Ratio:.2f} loops={Loop_Count} "
            "final={Final_Inserted} term={Termination}".format(**row)
        )

    term_counts = Counter(r["Termination"] for r in non_converged)
    print("\nNon-converged termination breakdown:")
    for term, count in term_counts.items():
        print(f"  {term}: {count}")


def main():
    parser = argparse.ArgumentParser(description="Analyze Bloom benchmark CSV output.")
    parser.add_argument("csv_path", help="Path to bloom_with_set_stats.csv")
    parser.add_argument("--top-n", type=int, default=10, help="Top N non-converged rows to show")
    args = parser.parse_args()

    rows = load_rows(args.csv_path)
    summarize(rows)
    report_non_converged(rows, args.top_n)


if __name__ == "__main__":
    main()
