# Changelog

All notable changes to this repository are documented here. The format
follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Because the
repo ships seven implementations of one frozen format, entries are grouped by
scope; format v1 outputs never change (see SPEC.md).

## 1.0.0 (unreleased)

Initial release.

### Specification
- Format v1: FF1 (NIST SP 800-38G, AES) over configurable alphabets with
  length staging, domain tweaks (`"dealcode/v1/" + domain`), a deterministic
  key rule (16/24/32-byte keys direct; everything else via
  `SHA-256("dealcode/v1/kdf" ‖ material)`), and a `2^63` counter bound.
- Shared conformance vectors: the 9 official NIST FF1 samples plus 26
  generated configs (796 valid vectors, 239 invalid codes, normalization,
  range counters, and 12 invalid configurations).
- Construction-time guards against the two most likely misconfigurations:
  a custom alphabet that case-insensitively matches a preset name, and a
  string key that equals a preset name (swapped arguments).

### Implementations
- Seven independently packaged, bit-identical implementations: Python
  (reference), TypeScript/JavaScript (ESM+CJS), Go, Java, Rust, C (OpenSSL),
  C++ (wrapping the C core). Each is zero-dependency beyond platform/audited
  crypto, immutable, and safe for concurrent use.
- Idiomatic error taxonomy per language (`ConfigError` / `RangeError` /
  `InvalidCodeError` equivalents); decode length-gates before normalizing,
  so oversized garbage is rejected without allocation.
- C: `dealcode_new_ex` with field-level diagnostics, `make install` +
  pkg-config; C++: `find_package(dealcode)` / `add_subdirectory` /
  FetchContent consumption, tests gated behind `DEALCODE_BUILD_TESTS`.

### Documentation
- Normative SPEC.md; bilingual (EN/KO) READMEs and design/philosophy docs;
  MkDocs site with per-language guides deployed to GitHub Pages.
