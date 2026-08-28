# Changelog

All notable changes to this repository are documented here. The format
follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Because the
repo ships seven implementations of one frozen format, entries are grouped by
scope; format v1 outputs never change (see SPEC.md).

## Unreleased

## 1.0.1 — 2026-08-28

Maintenance: dependency and toolchain refresh plus registry-page polish.
No changes to the code format — v1/v1c outputs are bit-identical to 1.0.0.

### Implementations
- Rust: `aes` 0.9 and `sha2` 0.11 (RustCrypto stable series); MSRV raised
  to 1.85; `Cargo.lock` now committed for reproducible CI builds.
- Java: test suite on JUnit 6; Maven plugin refresh (javadoc 3.12.0,
  compiler 3.15.0, surefire 3.5.6, gpg 3.2.8); `jackson-databind` 2.22.2
  (security update, test scope).
- JavaScript: `@types/node` 26 (dev-only).

### Registry pages & docs
- Package metadata on PyPI, npm, and crates.io links the documentation
  site (<https://algorix-hq.github.io/dealcode/>); README gains registry
  version badges. This release republishes every package with OIDC
  Trusted Publishing provenance and refreshed registry READMEs.

### Infrastructure
- CI runs automatically on pushes to `main` and on pull requests; docs
  deploy on pushes touching the site's sources.
- Dependabot with grouped minor/patch updates per ecosystem, an automerge
  chain for green Dependabot PRs, SHA-pinned publish actions, branch/tag
  protection rulesets, CODEOWNERS, and expanded RELEASING runbooks
  (release infrastructure, per-registry broken-release remedies, GPG key
  lifecycle).

## 1.0.0 — 2026-08-28

Initial release.

### Specification
- Fixed-length cycling mode (SPEC §11, tweak namespace `dealcode/v1c/`):
  PNR-style codes that never grow — when the fixed-length space is
  exhausted, the next cycle refills the same space through a different
  permutation. Conformance vectors in `testvectors/v1c.json`; implemented
  in all seven languages.
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
