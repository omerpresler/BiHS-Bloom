---
name: run-bihs-slurm
description: Build, launch, monitor, and validate BiHS-Bloom jobs on the configured Slurm cluster. Use for SSH cluster access, srun smoke tests, sbatch experiment pipelines, STP/Pancake/Rubik arrays, job status, scheduler failures, binary freshness, and Slurm log/result checks in this repository.
---

# Run BiHS Slurm

Operate the repository's cluster workflows without executing research binaries on a login node. Read `AGENTS.md` and [references/workflows.md](references/workflows.md) before submitting or changing a job.

## Establish scope

1. Identify the domain, instance range, algorithm/configuration, and whether the request is a short check or a long experiment.
2. Inspect the remote checkout, current commit, dirty files, active jobs, available partitions, and relevant artifacts.
3. Preserve existing jobs, logs, CSV shards, PDBs, and dirty worktree files unless the user explicitly authorizes changes.
4. If a request would launch the legacy all-instance script, overwrite results, consume substantial resources, or alter the scientific comparison, state that impact before submission.

## Select the workflow

- Use `srun` for builds, unit tests, smoke tests, and other short interactive checks.
- Use the repository's `sbatch` dependency pipeline for long experiments. An sbatch file must invoke the research executable with `srun`.
- Use `slurm/submit.sh` for calibrated split STP/Rubik runs.
- Use `slurm/stp_array.sbatch` for normal STP instances and `slurm/stp_high_memory.sbatch` for known high-memory instances 59, 81, and 87.
- Use `slurm/submit_stp_pancake.sh` for the combined 100 STP and 100 Pancake array.
- Treat `scripts/stp.sh`, `scripts/pancake.sh`, and `scripts/run_split_stp_local.sh` as local launchers. Never invoke them directly on a login node.

Do not assume a partition exists. Query `sinfo`/`scontrol`; use the configured `build` partition when present, otherwise explain the fallback before using an available CPU partition.

## Build safely

1. Confirm the remote commit contains the intended source changes.
2. Load `anaconda/anaconda`, initialize Conda in the non-interactive shell, and activate `bihs-build`.
3. Run the headless build with `OPENGL=STUB` inside an `srun` allocation.
4. Build `src/bin/release/stp_bihs_bloom`; package it into `slurm/bin/stp_bihs_bloom` only when the selected workflow consumes the packaged binary.
5. Compare timestamps/hashes and use `file` to reject Windows executables or stale binaries.
6. Do not create/update the Conda environment or download packages unless needed and authorized.

Never report the unit suite as current when `src/paper/stp_bihs_bloom/test.cpp` is absent. Use a clearly labeled integration smoke test or restore/add tests only with user authorization.

## Submit and monitor

Before submission, show the exact `srun` or `sbatch` command, requested partition, CPUs, memory, time, instance range, output paths, and whether files will be overwritten.

After submission:

1. Capture every job ID and dependency.
2. Monitor with `squeue`; reconcile terminal state and exit code with `sacct`.
3. Read both stdout and stderr from `logs/` or `results/logs/`.
4. Do not cancel or requeue jobs without explicit authorization.
5. Diagnose the first failing stage before retrying. Do not hide failed attempts.

## Validate results

For experiment outputs or suspicious runs, read `../review-bihs-logs/SKILL.md` and its logging-schema reference.

Require all of the following before calling a run successful:

- scheduler state `COMPLETED` and exit code `0:0`;
- empty or explained stderr;
- result status `ok` rather than a nonnegative runtime alone;
- valid solution length and agreement with the matched optimal baseline when available;
- CSV header/row width consistency and expected configuration coverage;
- convergence evidence for successful BiHS-Bloom runs;
- reported binary path and commit correspond to the intended build.

Report facts separately from hypotheses. Include job IDs, resource allocation, runtime, result status, solution length, scan counts when present, log paths, and any integrity caveats.
