# Configuration

A dealcode instance ("codec") is defined by four options plus the key. The
same configuration produces the same mapping in every language.

| Parameter    | Type    | Default        | Meaning |
|--------------|---------|----------------|---------|
| `key`        | bytes or string | — (required) | AES key material; see [Keys](#keys) |
| `alphabet`   | string  | `"hex"`        | preset name or custom alphabet |
| `min_length` | integer | `6`            | starting code length |
| `max_length` | integer | largest length whose full code space fits `2^63 − 1` | maximum code length |
| `domain`     | string  | `""`           | namespace label, bound into the FF1 tweak |

Invalid configuration is rejected at construction time (`ConfigError` or the
language's idiomatic equivalent) — never silently fixed. The exact
constraints live in the [specification](../spec.md).

## Alphabets

The character at index `i` represents numeral value `i`; codes are rendered
big-endian. Eight presets ship with sensible decode normalization:

| Name        | Radix | Characters (in order) | Decode normalization |
|-------------|-------|------------------------|----------------------|
| `dec`       | 10    | `0123456789`           | none |
| `hex`       | 16    | `0123456789abcdef`     | ASCII-lowercase input |
| `base32`    | 32    | `ABCDEFGHIJKLMNOPQRSTUVWXYZ234567` (RFC 4648) | ASCII-uppercase input |
| `crockford` | 32    | `0123456789ABCDEFGHJKMNPQRSTVWXYZ` (Crockford Base32) | ASCII-uppercase input, then map `O→0`, `I→1`, `L→1` |
| `base36`    | 36    | `0123456789abcdefghijklmnopqrstuvwxyz` | ASCII-lowercase input |
| `base58`    | 58    | `123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz` (Bitcoin) | none |
| `base62`    | 62    | `0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz` | none |
| `base64url` | 64    | `ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_` (RFC 4648 §5) | none |

Normalization applies to `decode` input only; `encode` always emits the
canonical characters. For human-typed codes, `crockford` is the friendly
choice: no confusable characters, and typos like `O` for `0` are mapped back
automatically.

!!! warning "Separators are not ignored"
    Unlike Crockford's original Base32 essay, dealcode does **not** skip
    hyphens or spaces: `decode("H4P-FG6")` is rejected. If you display
    codes grouped (`XXXX-XXXX`), strip the separators (and any surrounding
    whitespace) before decoding.

A **custom alphabet** is any string of 2–94 distinct printable ASCII
characters (`0x21`–`0x7E`, no spaces or control characters) — `"!@#$%^&*"`
works. Custom alphabets have no normalization: decode input must match
exactly.

## Domains

`domain` is a namespace label (`"orders"`, `"coupons"`, `"invites"`). Two
codecs with the same key but different domains produce **unrelated
permutations** — one key, unlimited independent code streams, which is
operationally much cheaper than one key per namespace. The domain is bound
into the FF1 tweak as `"dealcode/v1/" + domain`, so format v1 is also
separated from any future v2 and from any other FF1 use of the same key.

Constraints: valid Unicode (no U+0000, no unpaired surrogates), UTF-8 byte
length ≤ 255.

## Keys

Users hold keys in many shapes, and all are accepted with one deterministic
rule shared by every language:

- **Bytes** of length exactly 16, 24, or 32 → used directly as the AES key.
- **Any other non-empty bytes, and *all* strings** → expanded to an AES-256
  key: `SHA-256("dealcode/v1/kdf" ‖ material)`.
- Empty key material → `ConfigError`.

!!! warning "A hex-looking string is NOT hex-decoded"

    A string is *always* treated as its UTF-8 bytes and derived — even if it
    looks like hex. Pass the output of `openssl rand -hex 32` straight in as
    a string and every language derives the same AES-256 key from it. But if
    you hex-decode it yourself in one service and pass the string in
    another, you get **two different permutations**. Pick one form and use
    it everywhere. (The no-guessing rule is deliberate: auto-detection would
    make `"deadbeef..."` ambiguous.)

Derivation is domain separation, not password stretching: a passphrase key
is exactly as strong as the passphrase. Prefer ≥128-bit random material
(`openssl rand -hex 32`). String key material must be valid Unicode — U+0000
and unpaired surrogates are rejected rather than silently re-encoded, so
every language derives the same key or none does.

## Length staging

Codes start at `min_length` and grow one character at a time, only when the
current length is exhausted. With radix `r`, the first stage covers counters
`[0, r^min_length)`; stage `d` covers `[r^(d−1), r^d)`:

- `hex` with `min_length=6`: 16,777,216 six-character codes, then — only
  then — seven characters.
- Codes of different lengths trivially never collide; within one length FF1
  is a permutation; so the whole mapping is a bijection.

`min_length == max_length` gives fixed-length codes. The default
`max_length` is the largest length whose full code space is reachable by
signed-64-bit counters (hex → 15, dec → 18, base32/crockford/base36 → 12,
base58/base62/base64url → 10). You can raise it up to `r^max_length ≤ 2^128`
for long or fixed-length shapes (16-char hex, 12-char base62); counters
remain bounded by `2^63`, and the surplus code space is simply rejected by
decode.

Small first stages (down to `r^min_length ≥ 100`) are supported and
interoperable, but a tiny code space is trivially enumerable — see the
[security model](security.md).

### Fixed-length cycling mode

When codes must **never grow** — airline-PNR-style, always exactly `L`
characters — the cycling mode (SPEC §11) keeps the fixed length and, when
the space is exhausted, refills the *same* space through a **different
permutation** (cycle 1, cycle 2, …) instead of adding a character. Counter
`n` lives in cycle `n ÷ rᴸ`; each cycle issues every possible string exactly
once, in a new key-and-cycle-dependent order.

!!! danger "Codes repeat across cycles — by design"

    Reuse is the whole point, so the usual global `UNIQUE(code)` contract no
    longer holds across cycles. Keep at most one cycle's codes live per
    uniqueness scope (retire/expire before rolling over), index as
    `UNIQUE(cycle, code)`, and persist each live code's cycle — `decode`
    requires it (`decode(code, cycle)`), because the same string maps to a
    different counter in every cycle.

Constraints: `2 ≤ L ≤ 128`, `radix^L ≥ 100`, and `radix^L ≤ 2^63` (a cycle
must be completable within the counter space — for larger fixed shapes use
plain `min_length == max_length`).

## The configuration is frozen once shipped

!!! danger "Write-once configuration"

    For a given namespace (one counter sequence), the entire configuration —
    key, alphabet, `min_length`, `max_length`, `domain` — is **frozen the
    moment the first code ships**. A different configuration is a
    *different permutation*, and two permutations over one counter space
    can collide with already-issued codes.

    Need a new scheme? New domain (or new key + new namespace). Key
    rotation also means a new namespace — the old codes stay decodable only
    under the old configuration.

Corollaries: never feed two sequences into the same codec+domain, never
reset a sequence backwards, and keep the `UNIQUE` tripwire index described
in [Database integration](database.md).
