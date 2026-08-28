# Releasing

Maintainer checklist for shipping a release of all seven implementations.
Versions are kept in lock-step: `python/pyproject.toml` +
`python/src/dealcode/__init__.py`, `js/package.json`, `rust/Cargo.toml`,
`java/pom.xml`, `c/include/dealcode.h` (`DEALCODE_VERSION`),
`cpp/CMakeLists.txt` (`project(... VERSION ...)`) must all agree. Go has no
embedded version — it is versioned by the `go/vX.Y.Z` tag.

## Prerequisites (one-time)

- PyPI API token, and `build` + `twine` installed (`pip install build twine`).
- npm account with publish rights (`npm login`).
- crates.io token (`cargo login`).
- Maven Central Portal token configured as server id `central` in
  `~/.m2/settings.xml`, plus a GPG signing key.
- GitHub Pages source set to "GitHub Actions" (repo settings).

## Checklist

1. Update `CHANGELOG.md`: retitle the unreleased section to the version and
   date; start a new unreleased section.
2. **Remove the "not yet published" notices** — they are baked into the
   registry long-descriptions and must disappear in the same commit that gets
   tagged: root `README.md` / `README.ko.md` (implementations table note) and
   `python/README.md`, `js/README.md`, `java/README.md`, `rust/README.md`
   (install sections).
3. Bump all seven version strings listed above (skip for the first release).
4. Run everything locally (the CI jobs' commands per directory), then push
   and dispatch CI: `gh workflow run ci.yml -f only=all` — wait for green.
5. Tag and push: `git tag vX.Y.Z && git tag go/vX.Y.Z && git push origin
   vX.Y.Z go/vX.Y.Z`.
6. Publish, in any order:
   - PyPI: `gh workflow run publish-pypi.yml` (Trusted Publishing/OIDC —
     no credentials needed).
   - npm: `gh workflow run publish-npm.yml` (Trusted Publishing/OIDC).
   - Maven Central: `gh workflow run publish-maven.yml` (GPG key + Portal
     token live in the `maven-central` environment secrets), then press
     **Publish** in the Central Portal (`autoPublish` is deliberately off).
     Note: use plain `mvn package` for local artifacts — sources/javadoc
     jars are already attached by the pom; invoking `source:jar
     javadoc:jar` on top of `package` fails with "duplicated artifacts".
   - crates.io: `gh workflow run publish-crates.yml` (Trusted
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
