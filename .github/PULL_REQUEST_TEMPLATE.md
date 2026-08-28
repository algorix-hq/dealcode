## What

<!-- One paragraph: what changes and why. Link the issue if there is one. -->

## Checklist

- [ ] Tests pass locally for every implementation I touched (each
      directory's README shows how).
- [ ] No change to `encode` output or `decode` acceptance — or this PR bumps
      the format version and adds new vector files (v1 vectors are never
      edited; see CONTRIBUTING.md).
- [ ] Spec-affecting changes update `SPEC.md`, the Python reference, the
      regenerated vectors, and **all seven** implementations together.
- [ ] No new runtime dependencies (platform/audited crypto only).
