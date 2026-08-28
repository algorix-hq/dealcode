# Security Policy

## What dealcode is, security-wise

dealcode makes counters *look* random. It is *not* an authentication or
secrecy mechanism:

- Codes are **not tokens**. The code space is deliberately small; valid codes
  can be guessed at a rate proportional to `issued / capacity`. Rate-limit
  lookups and use ≥128-bit random tokens for anything security-critical.
- Do **not** encrypt confidential data with the FF1 core. FF1 on small
  domains has known distinguishing attacks; that is acceptable for obfuscating
  counters, not for protecting secrets.
- If the key leaks, the issue order of all past codes is revealed and every
  future valid code becomes enumerable. Treat the key like a production
  secret (KMS/Vault, per-environment keys).

See [SPEC.md §10](SPEC.md#10-security-model-informative) for the full model.

## Supported versions

Only the latest release of each implementation is supported with fixes.
Format v1 outputs are frozen; security fixes never change the code mapping.

## Reporting a vulnerability

Please **do not open a public issue** for suspected vulnerabilities
(key-material leakage into logs/errors, timing side channels that reveal
counters, memory-safety defects in `c/`/`cpp/`, spec deviations that break
the bijection).

Instead, use
[GitHub private vulnerability reporting](https://github.com/algorix-hq/dealcode/security/advisories/new)
on this repository. You should receive an initial response within 7 days.

Deviations between an implementation and `SPEC.md` that merely produce wrong
codes (without a security impact) are ordinary bugs — please open a public
issue for those.
