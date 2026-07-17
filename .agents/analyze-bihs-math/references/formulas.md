# BiHS-Bloom mathematical reference

Let `m` be filter bits, `n` inserted items, and `k` hash locations per item.

## Bloom formulas

- `m = size_kib * 1024 * 8`.
- Zero-bit probability: `(1 - 1/m)^(kn)`, approximately `exp(-kn/m)`.
- Expected fill: `q = 1 - (1 - 1/m)^(kn)`, approximately `1 - exp(-kn/m)`.
- Approximate false-positive probability: `p = q^k`, commonly `(1 - exp(-kn/m))^k`.
- Continuous optimum: `k* = (m/n) ln 2`; implementation rounds and clamps to at least one.
- At the continuous optimum, `p_min` is approximately `(0.6185)^(m/n)`.
- Occupancy cardinality estimate: `n_hat = -(m/k) ln(1 - X/m)` for `X` set bits; unstable near saturation.

The driver uses `fp_rate(k,n,m) = (1-exp(-kn/m))^k`. `optk` is rounded `(m/n)ln2`; `rootk` is the rounded square root of that rounded/clamped value; `k1` is one.

Repeated insertion does not behave like a new independent item. Verify the implementation's `n_inserted` semantics before substituting it for `n`.

## Experimental inference

- Compare matched instances; report solved fraction and failure modes alongside runtime/nodes.
- Runtime ratios are skewed: use per-instance ratios and geometric means only for positive solved pairs; also report medians/quantiles and useful uncertainty intervals.
- Timeout/OOM/skipped are censored or competing outcomes, not negative runtimes.
- Multiple ratios and modes create multiple comparisons; label exploratory analysis or control error rates for confirmatory claims.
- Disclose when parameters use baseline frontier information rather than independent evaluation.

## Search claims

An optimality/completeness argument must establish the bound/depth schedule, heuristic requirements, preservation of required meeting candidates through regeneration, exact equality at intersection, and correct path reconstruction. Bloom no-false-negatives alone does not establish these conditions.
