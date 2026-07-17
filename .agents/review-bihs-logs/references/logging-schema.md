# BiHS-Bloom logging schema

## Producers and flow

- `src/paper/stp_bihs_bloom/Driver.cpp` writes direct benchmark, convergence, calibration, and split result CSVs.
- `BiHSBloom.h::IterationStat` supplies convergence metrics.
- `slurm/*.sbatch` captures stdout/stderr under `results/logs/` and writes shards.
- `scripts/merge_split_results.py`, `slurm/merge_results.py`, and `slurm/merge_stp_pancake.py` merge shards.
- Runtime-report and convergence-plot scripts consume merged files.

## Split schemas

Result fields: `domain,instance,algorithm,ratio,status,time,nodes,necessary_nodes,storage_states,size_kib,k_hashes,fp_est,solution_length,k_mode,split_mode`.

Calibration fields: `domain,instance,algorithm,status,time,nodes,solution_length,memory_items,frontier_items`. Merged parameters add `min_memory_items`, `max_baseline_time`, and `reason`.

Statuses include `ok`, `timeout`, and `oom`, plus workflow-specific missing/unsupported states. Check time and solution length for consistency.

## Convergence schema

`instance,size_kib,ratio,total_depth,iteration,n_inserted,n_unique,estimated_fp,bits_set,fill_ratio,expected_fill_ratio,materialized_forward,materialized_backward,materialized_total,phase,type_index,type_count,k_mode,k_hashes,split_mode`

- `phase=iter` is ordinary Bloom refinement; `phase=type` is a dynamically split/materialized partition.
- `n_inserted` is the filter's recorded insertion count; do not assume exact cardinality.
- `materialized_*` counts exact states during extraction.
- Validate probabilities/ratios in `[0,1]`, nonnegative counts, positive `type_count`, and `0 <= type_index < type_count`.

## Legacy benchmark caveat

The first row is a wide header; `BIHS_PARAM` metadata rows precede ordinary instance rows. Current merged files contain duplicate IDTHS headers: twelve BiHS configurations vary three `k_mode` values per ratio, while IDTHS names encode only ratio. Parse by position or repair names explicitly; dataframe libraries may silently rename/overwrite them.

Current direct-run time sentinels are `-1` timeout, `-2` OOM, and `-3` skipped. Verify historical producers before applying current semantics.

## Integrity checklist

- Expected instances, ratios, modes, and shards exist.
- Merge keys did not overwrite retries/configurations.
- Row widths match the appropriate row type.
- Successful BiHS rows have convergence evidence and valid solution length.
- Optimal algorithms agree on solution length where expected.
- `size_kib` matches intended ratio/calibration inputs.
- `k_hashes` matches mode and frontier/memory inputs.
- Scheduler cancellation, timeout, OOM, and preemption are not mistaken for algorithm outcomes.
