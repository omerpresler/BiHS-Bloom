# BiHS-Bloom Slurm workflow map

Recheck cluster state and file contents before acting; this map records the repository layout as of 2026-07-17.

## Cluster and checkout

- SSH alias: `bgu-slurm`
- Checkout: `/home/omerpr/BiHS-Bloom`
- Conda module/environment: `anaconda/anaconda`, `bihs-build`
- Current cluster has `main`, `debug`, and CPU/GPU partitions but no partition named `build`. Query again before every submission.
- Slurm may report a request to `main` as execution on the site-routed `cpu` partition.

## Launcher classification

| Path | Purpose | Execution rule |
|---|---|---|
| `scripts/stp.sh` | Legacy full 100-instance STP run using default outputs | Never run on login; previously killed at instance 87 |
| `scripts/pancake.sh` | Legacy full Pancake run | Never run on login |
| `scripts/run_split_stp_local.sh` | Sequential calibration, phase 2, and merge | Local-only; wrap the whole workflow in an allocation only for a deliberate short range |
| `scripts/compile.sh` | Headless release build with existing compiler | Run inside an allocation on the cluster |
| `scripts/conda_compile.sh` | Creates/updates Conda build env, then builds | May mutate environment/download packages; do not use casually |
| `scripts/test.sh` | Invokes `make OPENGL=STUB test` | Currently blocked because `test.cpp` is absent |
| `slurm/submit.sh` | Split calibration-to-merge dependency chain | Preferred calibrated STP/Rubik pipeline |
| `slurm/stp_array.sbatch` | 97 independent STP instances at 64 GiB | Preferred normal STP array; excludes 59, 81, and 87 |
| `slurm/stp_high_memory.sbatch` | STP instances 59, 81, and 87 at 128 GiB | Submit separately from the normal STP array |
| `slurm/submit_stp_pancake.sh` | 100 STP + 100 Pancake tasks and merge | Preferred combined array |

Generated CSVs, `output.log`, `proof_unique.csv`, `STP_distance_*`, PNGs, `__pycache__`, and `slurm/bin/stp_bihs_bloom` are artifacts, not launch instructions.

## Build template

Run this shape through SSH after showing it to the user:

```bash
srun --partition=<available-cpu-partition> --job-name=bihs-build \
  --nodes=1 --ntasks=1 --cpus-per-task=<n> --mem=<memory> --time=<limit> \
  --output=logs/bihs_build_%j.out --error=logs/bihs_build_%j.err \
  bash -lc 'module load anaconda/anaconda &&
    source "$(conda info --base)/etc/profile.d/conda.sh" &&
    conda activate bihs-build &&
    cd /home/omerpr/BiHS-Bloom/src/build/SFML/paper/stp_bihs_bloom &&
    make -B -j<n> OPENGL=STUB release'
```

The project-specific build output is `src/bin/release/stp_bihs_bloom`. The split and array sbatch files mostly consume `slurm/bin/stp_bihs_bloom`, so package intentionally after a successful build and verify freshness.

## STP workflow choices

### Focused smoke test

Use the phase-run CLI with an existing valid params row:

```text
--stp --phase run --instance 0 --algorithm bihs_bloom --ratio 0.5
--k-mode optk --params-input results/split/params/stp_params.csv
```

The accepted algorithm labels are `bihs_bloom` and `idths_trans`; `bihs` is invalid.

### Calibrated split pipeline

`slurm/submit.sh` generates three calibration jobs per instance (`astar`, `rev_astar`, `mm`), merges params, generates six phase-2 rows per usable instance (three `optk` BiHS configurations and three IDTHSwTrans configurations at 50%, 10%, and 1%), runs them, then merges results.

Its current sbatch array directives are fixed for one instance:

- calibration: `0-2%3`
- phase 2: `0-5%6`

Do not widen `INSTANCE_END` without also making the array ranges cover all manifest rows.

### Full STP array

`slurm/stp_array.sbatch` maps array task `i` to exactly STP instance `i`, requests 64 GiB, and excludes the known OOM instances 59, 81, and 87. `slurm/stp_high_memory.sbatch` runs those three separately at 128 GiB. Both write:

- `results/parts/benchmark_<i>.csv`
- `results/parts/convergence_<i>.csv`
- `results/logs/stp_<array-job>_<task>.{out,err}`

The high-memory array uses `results/logs/stp_highmem_<array-job>_<task>.{out,err}`.

Submit the array directly with `sbatch`; each task launches the executable with `srun`. Use `slurm/merge_results.py` after structural validation of all shards.

### Combined STP and Pancake

`slurm/submit_stp_pancake.sh` maps tasks `0-99` to STP and `100-199` to Pancake, then runs `slurm/merge_stp_pancake.py`. `FULL_BASELINES=1` materially increases work.

## Split data flow

```text
generate_split_manifest.py (calibrate)
  -> stp_calibration.sbatch
  -> merge_params.sbatch
  -> stp_run_manifest.sbatch
  -> stp_run.sbatch
  -> merge.sbatch
```

Outputs live under `results/split/`; final STP files are `results/merged/benchmark_stp_korf100.csv` and `results/merged/bloom_convergence.csv`.

## Known traps

- `scripts/stp.sh` passes `--verbose`, but the current CLI does not parse that flag; the run still defaults to instances 0-99.
- The legacy script writes default filenames in its current working directory and truncates them at startup.
- `src/paper/stp_bihs_bloom/test.cpp` was deleted in commit `f74e009f`; stale objects must not be treated as a current unit test.
- A source-tree binary may require the Conda runtime environment. A packaged binary may be stale relative to HEAD.
- `slurm/merge.sbatch` uses `.e` rather than `.err` for its stderr suffix.
- `afterany` merge jobs can run after failed array tasks; a successful merge job does not prove complete experiment coverage.
- STP instances 59, 81, and 87 have exceeded 64 GiB. Keep them out of the normal array and run them with `slurm/stp_high_memory.sbatch` at 128 GiB.
- Preserve generated results and existing jobs. Never clean, overwrite, cancel, or resubmit without confirming scope.

## Completion checklist

1. `sacct`: terminal state and exit code for every stage/task.
2. stdout/stderr: no OOM, timeout, cancellation, preemption, assertion, or unsupported CLI label.
3. shard inventory: expected instances/configurations and no missing/duplicate keys.
4. result rows: `status=ok`, valid solution length, consistent schema.
5. BiHS convergence: present and structurally valid.
6. merged outputs: produced only after shard validation; report incomplete coverage explicitly.
