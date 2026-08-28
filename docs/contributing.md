---
title: Contributing
---

<!--
  This page renders CONTRIBUTING.md from the repository root verbatim — the
  file is the single source and is not duplicated here. The "Conformance in
  practice" section below is site-only.
-->

--8<-- "CONTRIBUTING.md"

---

## Conformance in practice

The bar for "this implementation is correct" is deliberately mechanical:
pass the files in
[`testvectors/`](https://github.com/algorix-hq/dealcode/tree/main/testvectors)
(`v1c.json` covers the fixed-length cycling mode, required for
implementations that ship it — all seven here do).

**`ff1_nist.json`** pins the FF1 core to the nine official NIST sample
vectors, independent of dealcode's own layer. If your FF1 is right, these
pass; if these pass, seven different FF1 implementations are provably
computing the same permutation. (NIST publications are U.S. public domain.)

**`v1.json`** exercises the full codec: every preset alphabet, stage
boundaries (the exact counters where codes grow a character), both
key-derivation paths (AES-sized bytes used directly vs. everything else
derived), decode normalization (`hex` case-folding, Crockford `O→0`,
`I/L→1`), domains, and codes that MUST be **rejected** — wrong length, wrong
charset, out-of-stage, out-of-counter-space. It is generated from the Python
reference implementation by
[`scripts/generate_test_vectors.py`](https://github.com/algorix-hq/dealcode/blob/main/scripts/generate_test_vectors.py);
counters are encoded as JSON strings because they exceed 2^53.

Every language's test suite consumes both files, so a port is conformant the
moment its suite is green — no judgment calls. This is also why v1 vectors
are frozen forever: they *are* the compatibility contract between
implementations, and between you and every code your users have already
been issued.
