# Contributing

Thanks for helping! The one thing to understand about this repository: the
product is a **bit-exact cross-language mapping**. Everything below follows
from that.

## Ground rules

- [`SPEC.md`](SPEC.md) is normative. Code follows the spec, never the other
  way around. If an implementation and the spec disagree, the implementation
  is wrong (or the spec needs an RFC — open an issue first).
- Any change that alters `encode` output or `decode` acceptance is a **new
  format version**, not a patch. Format v1 outputs are frozen forever.
- An implementation is conformant iff it passes the files in
  [`testvectors/`](testvectors/):
  - `ff1_nist.json` — the official NIST FF1 samples (validates the FF1 core),
  - `v1.json` — dealcode vectors (validates the full codec). This covers
    every section: `vectors`, `invalid_codes`, `normalize`, `range_counters`,
    and the top-level `invalid_configs` (see SPEC.md §9 for the exact
    obligations of each),
  - `v1c.json` — fixed-length cycling mode vectors (SPEC.md §11), required
    for implementations that ship the mode; all seven here do.

## Working on an implementation

Each directory (`python/`, `js/`, `go/`, `java/`, `rust/`, `c/`, `cpp/`) is a
self-contained, idiomatically packaged library. Its README shows how to run
its tests. Shared expectations:

- No new runtime dependencies. AES/SHA-256 come from the platform's standard
  or designated crypto library only.
- Public APIs stay small and mirrored across languages: construct a codec
  (key, alphabet, min/max length, domain) → `encode` / `decode` → three error
  kinds (config / range / invalid code).
- Codecs are immutable and safe for concurrent use.

## Changing the spec or vectors

1. Open an issue describing the change and its compatibility impact.
2. Spec text, the Python reference (`python/`), regenerated vectors
   (`python3 scripts/generate_test_vectors.py`), and updates to **all**
   implementations land in a single PR.
3. Output-affecting changes bump the format version (new tweak prefix
   `dealcode/v2/...`) and add new vector files next to the old ones — v1
   vectors are never edited.

## Adding a new language

Port from `SPEC.md` alone (peeking at `python/` is fine), make both vector
files pass in your test suite, add a README with a quickstart and a database
recipe, and wire a job into `.github/workflows/ci.yml` (including the new
name in the `only` input's `options` list). That's the whole bar.

## Branches & merging

Trunk-based: `main` is always releasable and is protected against
force-push and deletion; release tags (`v*`, `go/v*`) are immutable.
Contribute from a fork/feature branch via PR — squash-merged, branch
auto-deleted. Versions are SemVer, released in lock-step across all seven
packages (see RELEASING). Dependabot patch/minor updates auto-merge once
CI passes; majors wait for a human.

## CI

CI runs automatically on every push to `main` and on every pull request
(all seven implementations plus a vectors-reproducibility check). Please
still run your language's suite locally before pushing — it's faster
feedback than the runners. `gh workflow run ci.yml -f only=<job>` remains
available for one-off manual runs of a single job.

## Releasing (maintainers)

Registry publishing is automated per registry via the `publish-*.yml`
workflows; the full checklist, the credential/Trusted-Publisher
inventory, and hard-won bootstrap notes live in
[RELEASING.md](https://github.com/algorix-hq/dealcode/blob/main/RELEASING.md).

## License

MIT. By contributing you agree your contribution is MIT-licensed.
