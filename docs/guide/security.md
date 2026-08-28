# Security model

dealcode's threat model is narrow and deliberate: **don't let outsiders read
or enumerate your sequence.** It is not an encryption product and its codes
are not secrets. This page is the honest version of what the key does and
doesn't protect, condensed from [the specification](../spec.md) (§10) and
[the design notes](../design.md).

## What the key protects — and what it doesn't

**Uniqueness does not depend on the key being secret.** It follows from FF1
being a permutation — a structural property. What the key protects is
*unpredictability*: without it, codes reveal nothing about issue order or
volume. Sequential counters come out scattered; nobody can estimate your
production rate from the codes you hand out (the
[German tank problem](https://en.wikipedia.org/wiki/German_tank_problem)
dealcode exists to prevent).

## Codes are not authentication tokens

The code space is deliberately small — that is what makes codes short (16.7M
codes for `hex` at length 6). An online attacker who can try codes succeeds
at a rate proportional to `issued / capacity`.

- **Do not** use dealcode for session tokens, API keys, password-reset or
  magic-login links — anything that *authenticates by itself*. Use ≥128-bit
  CSPRNG tokens (`secrets.token_urlsafe(32)` and friends).
- **Do** rate-limit public code lookups as basic hygiene: small code space,
  so make online guessing boring.
- Remember that decode success only proves the code is *consistent* with the
  key — a well-formed unissued code decodes to some counter. The database
  lookup establishes existence; decode is parsing.

## Do not encrypt data with it

FF1 on small domains has known distinguishing attacks far below AES security
margins. For dealcode's obfuscation purpose this is acceptable — the
alternative permutations are worse in other ways, and anything
cryptographically stronger would still leave the small code space enumerable
online (mitigated by rate limiting, not cryptography). But do **not** use
this library to encrypt *confidential data*. It obfuscates a counter; that
is all.

## If the key leaks

The full issue order of all past codes is revealed — every code's position in
your sequence becomes public. Uniqueness is unaffected (it never depended on
secrecy), but the unlinkability you adopted dealcode for is gone for
everything already issued.

Treat the key like any other production secret:

- KMS/Vault, per-environment keys.
- Rotation = **new namespace** — the configuration, key included, is frozen
  for a live namespace ([why](configuration.md#the-configuration-is-frozen-once-shipped)).
  Old codes stay decodable only under the old configuration.

## Passphrase keys

Key derivation (`SHA-256("dealcode/v1/kdf" ‖ material)`) is domain
separation, not password stretching. A passphrase key is exactly as strong as
the passphrase. Prefer ≥128-bit random material: `openssl rand -hex 32`.

## Small code spaces

Configurations down to `radix^min_length ≥ 100` (FF1's structural minimum)
are supported and interoperable — supporting corner cases beats documenting
them away. But understand what you're choosing: NIST SP 800-38G Rev. 1
recommends domains of at least one million, and a 3-digit decimal code space
is trivially enumerable by anyone, key or no key. Pick a first stage sized
for your exposure.
