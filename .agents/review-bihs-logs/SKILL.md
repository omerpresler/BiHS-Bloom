---
name: review-bihs-logs
description: Review BiHS-Bloom experiment outputs, SLURM stdout/stderr, benchmark CSVs, convergence CSVs, calibration shards, merged results, and report inputs. Use when asked to inspect logs, diagnose failed or suspicious runs, validate result integrity, compare configurations, explain convergence, or decide whether thesis measurements are trustworthy.
---

# Review BiHS-Bloom Logs

Read `AGENTS.md`, then read `references/logging-schema.md` before interpreting data.

## Workflow

1. Identify whether this is a direct benchmark, split two-phase run, fixed Rubik run, or raw SLURM job.
2. Preserve source artifacts. Do not edit or merge results unless explicitly requested.
3. Inventory files, sizes, timestamps, headers, row counts, and instance/configuration coverage.
4. Validate structure before statistics: detect duplicate headers, inconsistent widths, repeated keys, missing shards, mixed schemas, truncation, nonnumeric values, and sentinels. Treat legacy `BIHS_PARAM` rows separately. Never trust automatic duplicate-column renaming.
5. Reconcile scheduler stdout/stderr, result status/sentinel, convergence trace, and solution length.
6. Group by domain, instance, ratio, `k_mode`, `k_hashes`, and `split_mode`; compare only matched instances.
7. Review convergence by `(instance, ratio, k_mode, split_mode, total_depth, phase, type_index, type_count)` in iteration order.
8. Flag invalid data, suspicious behavior, and consistent failures separately, with exact file/row evidence.
9. Trace suspicious fields to `Driver.cpp` or `BiHSBloom.h` before assigning a cause. Separate facts from hypotheses.

## Interpretation rules

- A Bloom hit is not an exact match; `n_unique` and materialized states are not interchangeable.
- In split output, status is authoritative. In current legacy output, time sentinels are `-1 = timeout`, `-2 = OOM`, and `-3 = skipped`.
- Nonnegative time alone does not prove correctness; require success/convergence and a valid solution signal.
- `estimated_fp` is model-based, not an observed false-positive fraction.
- Compare `fill_ratio = bits_set / m` with `expected_fill_ratio`, allowing for rounding and repeated insertions.
- Node counts may use different expansion semantics across algorithms; verify before claiming equivalence.
- Qualify Rubik results because heuristic quality is limited without a strong PDB.

## Output

Lead with whether the run set is usable. Report coverage, failures, integrity findings, convergence behavior, matched configuration comparisons, and recommended reruns/checks. Include denominators. Do not claim significance without an appropriate test and assumptions.
