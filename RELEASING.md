# Releasing

Maintainer checklist for shipping a release of all seven implementations.
Packages follow SemVer and move in **lock-step**: a release bumps every
package to the same version, even languages without changes. Version
locations: `python/pyproject.toml` + `python/src/dealcode/__init__.py`,
`js/package.json` **and** `js/package-lock.json` (bump with
`cd js && npm version X.Y.Z --no-git-tag-version`, which updates both),
`rust/Cargo.toml` **and** `rust/Cargo.lock` (refresh with
`cd rust && cargo update -w` after bumping the manifest), `java/pom.xml`,
`c/include/dealcode.h` (`DEALCODE_VERSION`), `cpp/CMakeLists.txt`
(`project(... VERSION ...)`).
Go has no embedded version — it is versioned by the `go/vX.Y.Z` tag.

## Release infrastructure (already set up — reference)

Nothing needs to be installed or logged in for a normal release; every
registry publishes from GitHub Actions. What exists and where:

| Registry | Workflow | Auth | GitHub environment |
|---|---|---|---|
| PyPI | `publish-pypi.yml` | Trusted Publishing (OIDC) — no credentials | `pypi` (no secrets) |
| npm | `publish-npm.yml` | Trusted Publishing (OIDC) — no credentials | `npm` (no secrets) |
| crates.io | `publish-crates.yml` | Trusted Publishing (OIDC) — no credentials | `crates-io` (no secrets) |
| Maven Central | `publish-maven.yml` | GPG key + Portal token from secrets | `maven-central` (3 secrets) |

- **Trusted Publisher registrations** (on each registry's settings page,
  all pointing at `algorix-hq/dealcode` + the workflow filename + the
  environment name above). If the repo is renamed, a workflow file is
  renamed, or an environment changes, re-register there — the registry
  matches these values exactly.
- **`maven-central` secrets**: `MAVEN_GPG_PRIVATE_KEY` (armored private
  key, no passphrase), `CENTRAL_TOKEN_USERNAME` / `CENTRAL_TOKEN_PASSWORD`
  (Central Portal user token, name `dealcode-github-actions`).
- **GPG signing key**: RSA-4096, fingerprint
  `19D3 8E7A B544 83B0 F591 9413 F9C1 29AC 84DB 98F2`, **expires
  2028-08-27**. Public key lives on keyserver.ubuntu.com and
  keys.openpgp.org (Central verifies against them); the private key and
  its revocation certificate are backed up in the maintainer's password
  manager. Before expiry: `gpg --edit-key 84DB98F2` → `expire` → `save`, re-upload
  the public key to both keyservers, update the environment secret.
  **If the key is compromised** (not just expiring): import the revocation
  certificate from the password manager (`gpg --import revoke.rev`) and
  `--send-keys` it to both keyservers (that publishes the revocation);
  generate a replacement (`gpg --quick-generate-key "<uid>" rsa4096 sign
  2y`) plus a new revocation cert (back into the password manager); upload
  the new public key to both keyservers **before** the next deploy;
  replace `MAVEN_GPG_PRIVATE_KEY`; update the fingerprint and expiry
  recorded in this file.
- **Rotating the Central token**: generate a new user token in the Portal
  (this invalidates the old one), then
  `gh secret set CENTRAL_TOKEN_USERNAME/-PASSWORD --env maven-central`.
- GitHub Pages source is set to "GitHub Actions" (repo settings).

Bootstrap quirks recorded from the 1.0.0 release, for whoever does this
next from scratch:

- npm and crates.io can only attach a Trusted Publisher to an **existing**
  package/crate — a first-ever publish is manual. npm's 2FA web-auth flow
  needs a real TTY (`npm publish` from an interactive shell); tokens do
  not bypass publish 2FA anymore.
- Maven Central rejected an ed25519 signing key ("could not find a public
  key by the key fingerprint") — use RSA-4096, upload the public key to
  the keyservers **before** deploying, and allow a few minutes'
  propagation.
- `central-publishing-maven-plugin` must be ≥ 0.11.0 (older versions
  crash on the Portal API's `warnings` field).

**Accounts** (outside-repo ownership; credentials + 2FA recovery live in
the maintainer's password manager): PyPI, npm, crates.io (GitHub-linked),
and Sonatype Central Portal (owns the `io.algorix` namespace) are all
individual maintainer accounts today — Trusted Publishing removes tokens
from releases, but re-registering publishers, yanking, and deprecating
still require these accounts. GitHub org/repo admin access (environments,
secrets, rulesets, Pages) is the fifth account in that list.

## If a published release is broken

Prefer shipping a fixed X.Y.Z+1 immediately; per registry:

- PyPI: **yank** it (project page → Manage → release → Options → Yank) —
  yanked releases are skipped by resolvers unless pinned `==X.Y.Z`.
- npm: `npm deprecate dealcode@X.Y.Z "broken — use X.Y.Z+1"` (unpublish
  only works within 72 h).
- crates.io: `cargo yank --version X.Y.Z` from `rust/` (`--undo`
  reverses); existing lockfiles keep resolving it.
- Maven Central: cannot be deleted after publish — the fixed release is
  the only remedy.
- Go: the proxy caches forever — add `retract vX.Y.Z` to `go/go.mod` and
  tag `go/vX.Y.Z+1`.

## Checklist

1. Update `CHANGELOG.md`: retitle the unreleased section to the version and
   date; start a new unreleased section.
2. Update user-facing version strings beyond the seven package files:
   `grep -rnF '<old-version>' README.md README.ko.md docs java/README.md
   c/README.md cpp/README.md` — Maven install snippets, the cpp
   FetchContent `GIT_TAG`, the C header prose, and the "vX.Y.Z is live"
   release callouts all hard-code the version.
3. Bump all the version locations listed above.
4. Run everything locally (the commands are in the `jobs:` of
   `.github/workflows/ci.yml`), then push — CI runs automatically on the
   push to `main`; wait for green.
5. Tag and push: `git tag vX.Y.Z && git tag go/vX.Y.Z && git push origin
   vX.Y.Z go/vX.Y.Z`.
6. Publish, in any order — **always dispatch against the tag**
   (`--ref vX.Y.Z`), so a commit landing on `main` between tagging and
   publishing cannot slip into the artifacts:
   - PyPI: `gh workflow run publish-pypi.yml --ref vX.Y.Z` (Trusted
     Publishing/OIDC — no credentials needed).
   - npm: `gh workflow run publish-npm.yml --ref vX.Y.Z` (Trusted
     Publishing/OIDC).
   - Maven Central: `gh workflow run publish-maven.yml --ref vX.Y.Z` (GPG key + Portal
     token live in the `maven-central` environment secrets), then press
     **Publish** in the Central Portal (`autoPublish` is deliberately off).
     Note: use plain `mvn package` for local artifacts — sources/javadoc
     jars are already attached by the pom; invoking `source:jar
     javadoc:jar` on top of `package` fails with "duplicated artifacts".
   - crates.io: `gh workflow run publish-crates.yml --ref vX.Y.Z` (Trusted
     Publishing/OIDC).
   - Go: nothing beyond the `go/vX.Y.Z` tag. Optionally warm the proxy:
     `GOPROXY=https://proxy.golang.org go list -m
     github.com/algorix-hq/dealcode/go@vX.Y.Z`
   - C / C++: no registry — the tag is the release (`make install`,
     vendoring, or CMake).
7. Deploy docs: `gh workflow run docs.yml`.
8. GitHub release: `gh release create vX.Y.Z --title "dealcode vX.Y.Z"
   --notes "<the CHANGELOG section>"`.
9. Sanity-check the registry pages (README rendering, license, links) and
   `pip install dealcode` / `npm install dealcode` / `cargo add dealcode` /
   the Maven coordinates in a scratch project.
