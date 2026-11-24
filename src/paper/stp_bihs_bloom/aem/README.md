# BiHS-Bloom-AEM: Adaptive Exact Materialization

This is a separate research variant, based on the immutable fresh-byte snapshot
of 16 October 2026. It does not modify `BiHSBloom.h`, the fresh campaign sources,
existing binaries, defaults, or result files. Snapshot dependency hashes are in
`snapshot_provenance.json`. Build through the existing headless project Makefile.

## Explicit algorithm identities

- `bihs_bloom`: unchanged legacy behavior in this isolated driver, retained for
  smoke equivalence and reference. Its historical logical budget does not bound
  simultaneous filters or exact-table auxiliary storage.
- `bihs_bloom_aem_control`: the bounded AEM storage implementation, attempting
  materialization using insertion-count <= actual exact capacity only.
- `bihs_bloom_aem`: the same storage implementation, adding an occupancy trigger
  `estimated_distinct <= alpha * actual_exact_capacity`. Default alpha is 0.5.

The principal comparison is AEM versus its matched storage control, not a claim
that the old logical-budget experiments have the same physical storage layout.
An explicit AEM name, `--memory-bytes`, positive `--bihs-timeout-seconds`, and
`--aem-output` are required. Alpha must be finite and strictly between zero and
one. AEM and streaming cannot be combined in this first variant.

The 15-puzzle pilot opts into `--aem-exact-batches` in both arms. This
allows alpha 1.0. When the next distinct candidate cannot fit, the algorithm
scans the opposite frontier against the full table. After a miss, it clears
the table and resumes materialization at the pending candidate. It scans
the final nonempty batch too. The summary's `table_full_scans` counts
opposite scans triggered by full batches in this mode. The historical
Hanoi pilot used the Bloom fallback described below.

With `--aem-exact-batches`, `--aem-half-bloom` assigns each live filter
half the managed budget as bit-array payload, rounded down.
Two filters can coexist during refinement. Exact batching holds only the
filter used to screen materialized states and uses the remaining bytes for
the exact table. During type splitting, building a partition's filter
consumes the seed. If another partition is needed, the original forward and
backward scans rebuild the seed with the same hash seeds. Rebuild work is
charged to search time and scan counts. The summary reports
`aem_seed_rebuilds` for this layout.

## Transition, correctness and fallback

After each completed Bloom scan, estimate distinct insertions using
`-m/k * log1p(-bits_set/m)`. Full filters have no usable occupancy estimate;
the count-based opportunity remains available. Materialize from the origin and
depth of the filter just built. A false-positive Bloom hit is only a candidate:
the table checks exact state equality, and intersection is exact. Returned moves
are replayed in the driver against the original endpoints.

Materialization regenerates frontier candidates; it does not traverse Bloom bits
as though they encoded states. No traversal-context cache or cycle-pruning change
is included. Consequently AEM can still spend substantial time regenerating a
frontier with many duplicate paths, even if the distinct set fits easily.

The table is a fixed open-address array with two slots per allowed exact entry,
plus a preallocated fixed-depth path arena. Duplicate insertion does not consume
a second entry, including when the table is full. Hash collisions retain every
distinct state. TOH uses a lossless two-bits-per-disk rank (32 bits for 14 disks);
other domains retain their full state value. No 64-bit fingerprint is accepted
as proof of Rubik or other state equality.

Before adding a new distinct state beyond capacity, throw a private table-full
signal, release the partial exact storage, and continue the Bloom workflow.
Overflow never discards surviving candidates or declares the bound exhausted.
Materialization works on endpoint copies so an interrupted recursive scan cannot
leave the caller's start/goal mutated. Empty or fully searched exact sets resolve
the current bound. After overflow, retry that direction only when the estimated
set has at least halved; a new bound/type restarts that retry gate.

## Memory definition

The supplied byte budget bounds **managed frontier storage**, including live
Bloom bit arrays, the exact table object, occupied and unoccupied slots,
and the full per-entry path arena. The default layout assigns each bit array
`budget / 4` bytes, rounded down. Bloom filter object headers are excluded from
managed storage and still contribute to process memory.
The common refinement path holds one or two filters; seeded type processing can
hold more. Every allocation is checked against actual live managed storage.
The exact capacity is recalculated from remaining bytes and path depth before
each attempt. The occupancy estimate does not relax that capacity.

PDBs, DFS stacks/scratch, returned solution vectors, instrumentation and allocator
bookkeeping are outside this managed budget. Total process RSS is measured by
`/usr/bin/time -v`; the Slurm process allocation remains 16 GiB. This is not a
claim that total process RAM is bounded by the managed frontier budget. Budgets
too small for the bit arrays are recorded as `budget_too_small`
without increasing the requested budget.

## Output and interpretation

Outputs are new files under `results/toh_aem_20261024/`. The isolated summary adds
explicit AEM identity, alpha, trigger mode, attempts, overflows, actual Bloom
buffer bytes and peak managed bytes. `aem_events.csv` records every attempt's
bound, direction, type, insertion attempts, set bits, occupancy estimate, actual
capacity, observed exact states, outcome, work and time. A timeout preserves
partial exact counts and event state. Legacy `peak_estimated_unique` remains an
insertion-count alias for snapshot compatibility; use the AEM event estimate.
Likewise the legacy summary `exact_state_capacity` is the old logical reference;
the event's `exact_capacity` is the enforced depth-dependent capacity.

## TOH experiment

Runner: `scripts/toh_aem_20261024.py`. Preparation creates exclusive manifests
and records binary, source, launcher and calibration hashes. The worker checks
those hashes and writes each run into a new directory. Neither preparation nor
printing a plan submits jobs.

All stages retain the same 50 deterministic 14-disk, four-peg endpoint pairs,
PDB10+4/PDB12+2, endpoint PDB construction, consistency enabled, and Bloom seed
20260824. Each byte budget is `floor(best_um_states * 28 * ratio / 8)` from the
existing exact A*/reverse-A*/MM calibrations. Both arms use the same rounded
MM-informed hash-count rule, computed from the **actual smaller Bloom array**.
This rule's input differs explicitly from the old full-array budget campaign.

1. **Smoke:** known easy TOH instance 19/PDB12+2 at 1 MiB, checking all three
   algorithm identities, optimal length 40, replay, managed storage and exact
   equivalence of the legacy work counters to the archived binary. Also run a
   one-second deadline check on instance 0.
2. **Paired pilot (48 runs):** instances 0,1,2,10,19,34; both PDB partitions;
   ratios 50% and 1%; control versus AEM(alpha=0.5); 30 minutes per search.
3. **Alpha sensitivity (24 runs):** instances 0,10,19,34; both partitions;
   ratio 10%; alpha 0.25/0.5/0.75; 30 minutes per search. Exploratory, with
   alpha=0.5 still preregistered for the main experiment.
4. **Main (600 runs):** all 50 instances; both partitions; ratios 50%,10%,1%;
   control versus AEM(alpha=0.5); 12 hours per search. Submit only after pilot
   audit. Pilot/sensitivity runs are not pooled with the longer-deadline main.

Instances in the pilot include the original failures 0,1,2 and the three
previously solved instances 10,19,34; the main population includes all 50.
Manifest order is shuffled with fixed seed 20261024. Single-seed results do not
establish seed robustness. No assertion that AEM will solve the failed cases is
made: this experiment isolates materialization policy from traversal caching.

Primary endpoints: solved/timeout/too-small/failure counts over every planned
configuration, matched search PAR-2 (twice the deadline on internal timeout),
then attempts, overflows, time spent in attempts, work, table size and managed/RSS
peaks. Setup time is separate. Outer watchdog failures and scheduler/OOM failures
are retained as failures and excluded from search PAR-2, not relabeled as search
timeouts. Successful solution lengths must agree with independent calibration.
Do not report a performance winner from only the common solved subset.

Each array task requests one CPU, 16 GiB; scheduler deadlines are 45 minutes for
pilot/sensitivity and 12h15m for main. The process watchdog is search deadline +
5 minutes, followed by at most one minute termination grace. Array specs contain
no throttle. Logs go under `logs/`. Recheck partitions before submission; use
`build` when available, otherwise document the available CPU fallback.
