# Project Context

## Purpose and ownership

This repository supports a master's thesis on bidirectional heuristic search in limited-memory environments. It is based on HOG2 (Hierarchical Open Graph 2), but the thesis contribution in this fork is **BiHS-Bloom**. Treat most of the repository as upstream HOG2 infrastructure unless a task clearly concerns the thesis implementation.

The primary research code is under `src/paper/stp_bihs_bloom/`. Experiment launchers are under `scripts/` and `slurm/`, and generated measurements and reports are under `results/`.

## Research idea

BiHS-Bloom is a memory-bounded meet-in-the-middle search. A conventional bidirectional search can require too much memory to store its frontier explicitly. BiHS-Bloom instead represents frontier candidates in a fixed-size Bloom filter. Bloom-filter membership is probabilistic: false positives may preserve extra candidates, but there must be no false negatives for inserted items.

At a high level, the algorithm repeatedly searches/rebuilds depth-bounded frontiers through the Bloom representation, aiming for the surviving candidate set to shrink. Once the candidate set is small enough to fit in the available exact-state budget, the algorithm materializes exact frontier states and searches for an exact intersection from the opposite direction to reconstruct and validate a solution path.

This is a working description of the implementation, not a license to change its invariants. Before modifying algorithmic behavior, inspect `BiHSBloom.h`, `Bloom.h`, `Bloom.cpp`, and `BloomUtil.h`, and state any assumptions that are not established by the code or thesis author.

## Important files

- `src/paper/stp_bihs_bloom/BiHSBloom.h`: main templated BiHS-Bloom search, frontier construction, convergence/termination, exact-state materialization, intersection, and path extraction.
- `src/paper/stp_bihs_bloom/Bloom.h` and `Bloom.cpp`: Bloom-filter implementation and stable state fingerprints.
- `src/paper/stp_bihs_bloom/BloomUtil.h`: Bloom-filter calculations/utilities.
- `src/paper/stp_bihs_bloom/Driver.cpp` and `Driver.h`: command-line modes, domains, baselines, experiment configuration, and CSV output.
- `src/paper/stp_bihs_bloom/IDBiHS.h` and `IDTHSwTrans.h`: comparison/baseline algorithms; do not confuse these with the thesis contribution.
- `src/build/SFML/paper/stp_bihs_bloom/`: project-specific build directory.
- `scripts/` and `slurm/`: local and cluster experiment orchestration.
- `results/`: generated experimental data, logs, plots, and reports; avoid rewriting these unless the task explicitly concerns results.

## Supported research domains

The research domains are the sliding-tile puzzle (STP), Pancake Puzzle, and Rubik's Cube. STP and Pancake are the primary practical experimental domains. Rubik's Cube support exists in the implementation, but experiments are currently limited by the lack of a sufficiently strong pattern database (PDB) heuristic; do not assume Rubik results are directly usable or comparable without addressing that heuristic limitation.

Domain-specific state hashing must remain stable and consistent between Bloom insertion, membership queries, frontier regeneration, and exact intersection. Incremental hashes must equal hashes recomputed from the resulting state.

## Correctness and research constraints

- Preserve the configured memory bound. Account for both the Bloom filter and any materialized exact-state structures when reasoning about memory.
- Never treat a Bloom-filter hit as proof of state equality. Exact intersection and path validation are required.
- Preserve the Bloom-filter no-false-negative assumption for inserted hashes. Changes to hashing, filter clearing/rebuilding, type splitting, or frontier generation require special care.
- Keep forward and backward depths, heuristic bounds, action inversion, and path orientation consistent. Every returned path must transform the start state into the goal state.
- Preserve deterministic stable fingerprints across insertion and regeneration. Do not replace them with implementation-defined `std::hash` behavior without explicit justification.
- Distinguish inserted-item counts, estimated/unique candidates, set bits, fill ratio, estimated false-positive rate, and materialized exact states in analysis and CSV output.
- Do not silently change experimental defaults, benchmark instances, timeout rules, memory ratios, hash-count selection, CSV schemas, or baseline behavior. These affect thesis reproducibility.
- Treat dynamic/type splitting and convergence termination as research behavior, not generic refactoring details.

## Working practices

- Read the local code before relying on textbook descriptions; the thesis algorithm is evolving and implementation details matter.
- Keep changes focused on `src/paper/stp_bihs_bloom/` and its experiment/build files unless an upstream HOG2 change is genuinely necessary.
- Avoid broad formatting or modernization changes across HOG2.
- Do not commit generated binaries, large logs, or experiment output unless explicitly requested.
- For algorithm changes, add or run focused checks for path validity, stable/incremental hash agreement, Bloom membership after insertion, memory accounting, and deterministic output.
- Build the headless research target from `src/build/SFML/paper/stp_bihs_bloom` with `make OPENGL=STUB`; use the repository's existing scripts for full experiments only when requested, since they may be expensive.

## Collaboration expectations

The repository owner is a master's student and may provide algorithm details informally. Help translate those details into precise terminology, invariants, tests, and reproducible experiments. When a request could alter the scientific meaning of BiHS-Bloom or invalidate comparisons, explain the implication and confirm the intended interpretation before making that change.

## Repository specialists

- Use `.agents/review-bihs-logs` when reviewing experiment logs, CSV integrity, failures, convergence traces, or result usability.
- Use `.agents/analyze-bihs-math` for Bloom-filter derivations, correctness assumptions, complexity, experiment statistics, or thesis mathematical claims.
- Use `.agents/run-bihs-slurm` for cluster builds, `srun`/`sbatch` launches, job monitoring, binary freshness, and Slurm workflow selection.

# Cluster execution rules

This repository runs on a Slurm cluster.

- Do not run computationally expensive programs directly on the login node.
- Use `srun` for short interactive tests.
- Use `sbatch` for long-running experiments.
- Use the `build` partition unless another partition is explicitly requested.
- Store job logs under `logs/`.
- Before submitting a job, show the exact `srun` or `sbatch` command.
- Do not cancel existing jobs unless explicitly requested.
