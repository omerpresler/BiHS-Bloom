---
name: analyze-bihs-math
description: Analyze and verify mathematics for BiHS-Bloom, Bloom filters, bounded-memory bidirectional heuristic search, convergence metrics, hash-count selection, complexity, experiment design, and statistical comparisons. Use when asked to derive formulas, check proofs or assumptions, explain observed behavior, design measurements, or review thesis mathematical claims.
---

# Analyze BiHS-Bloom Mathematics

Read `AGENTS.md`. For Bloom or experiment calculations, read `references/formulas.md`. Inspect the exact code path before applying an idealized formula.

## Method

1. State definitions, units, assumptions, and the exact claim.
2. Separate deterministic search correctness, probabilistic Bloom behavior, and empirical inference.
3. Derive from first principles with auditable algebra.
4. Map symbols to code fields such as `size_kib`, `k_hashes`, insertions, bits set, ratio, and frontier estimate.
5. Check zero/small `n`, integer rounding, saturation, repeated insertions, hash dependence, type splitting, and overflow.
6. Distinguish estimates, observed quantities, and exact counts; quantify approximation error when material.
7. Use matched instances for comparisons and report failures separately to avoid survivor bias.
8. State what evidence would falsify the conclusion.

## Guardrails

- Standard Bloom filters have no false negatives absent implementation/hash/reconstruction errors.
- Bloom false positives do not imply incorrect solutions; exact intersection/path validation is separate.
- State the independent-uniform-hash assumption when used; this code uses deterministic fingerprints and derived locations.
- Expected false-positive rate is not an observed rate.
- Total memory includes exact maps, paths, stacks, and auxiliary structures, not only Bloom bits.
- Empirical success alone does not prove optimality or completeness.
- Qualify Rubik conclusions without a strong PDB.

## Output

Give the result first, then definitions, derivation/check, assumptions, and implementation/experiment implications. Use exact inputs when known; otherwise give a symbolic result and identify missing measurements.
