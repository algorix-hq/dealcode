# Dealcode Specification — format version 1

Status: **stable**. Any change that alters the output of `encode` or the
acceptance behaviour of `decode` requires a new format version.

This document is the single source of truth. A conforming implementation can
be written from this document alone, and MUST pass every case in
[`testvectors/`](testvectors/).

## 1. Overview

Dealcode maps a non-negative integer counter `n` (from a database sequence or
any other source that never repeats) to a short, fixed-alphabet,
random-looking string called a **code**, and back. The mapping is a bijection
(a keyed permutation), so:

- Two different counters can never produce the same code. Uniqueness of codes
  reduces entirely to uniqueness of counters.
- A code can be decoded back to its counter by anyone holding the key.
- Without the key, codes carry no usable order/volume information.

The permutation is FF1 format-preserving encryption as specified in
NIST SP 800-38G. Dealcode adds: an alphabet layer, a length-staging scheme
(codes start short and grow one character at a time only when the current
length is exhausted), tweak derivation, and validation rules.

## 2. Configuration

A dealcode instance ("codec") is defined by:

| Parameter    | Type    | Default        | Constraints |
|--------------|---------|----------------|-------------|
| `key`        | bytes or string | — (required) | Any non-empty key material; see §2.1 |
| `alphabet`   | string  | `"hex"`        | A preset name (§3) or a custom alphabet (§3.2) |
| `min_length` | integer | `6`            | `min_length ≥ 2` and `radix^min_length ≥ 100` |
| `max_length` | integer | largest `L` with `radix^L ≤ 2^63 − 1` | `min_length ≤ max_length` and `radix^max_length ≤ 2^128` |
| `domain`     | string  | `""`           | UTF-8; encoded byte length ≤ 255 |

`radix` is the number of characters in the alphabet.

**Counter space.** Encodable counters are exactly
`0 ≤ n < min(radix^max_length, 2^63)`. The `2^63` bound is part of this
specification — counters are signed-64-bit-safe in every language, and every
implementation accepts/rejects exactly the same values. `radix^max_length`
MAY exceed `2^63` (up to `2^128`): that supports long or fixed-length code
shapes (e.g. 16-char hex, 12-char base62) whose code space is larger than the
counter space; the surplus code strings simply never occur and are rejected
by decode (§7).

Default `max_length` is the **largest** integer `L` such that
`radix^L ≤ 2^63 − 1` — the largest length whose full code space is reachable
by counters — but never less than `min_length` (so `min_length = 16` with hex
defaults to `max_length = 16`). Examples: hex → 15, dec → 18,
base32/crockford/base36 → 12, base58/base62/base64url → 10.

`radix^min_length ≥ 100` is FF1's structural minimum domain size
(NIST SP 800-38G). Note that NIST SP 800-38G **Rev. 1** recommends domains of
at least one million; smaller first stages (e.g. 4-digit decimal codes) are
supported and interoperable, but understand that tiny code spaces are
trivially enumerable (§10).

`domain` is an application-chosen namespace label (e.g. `"orders"`,
`"coupons"`). Two codecs with the same key but different domains produce
unrelated permutations. It is bound into the FF1 tweak (§5).

Setting `min_length == max_length` yields fixed-length codes.

**Immutability rule.** For a given code namespace (one counter sequence), the
entire configuration — key, alphabet, `min_length`, `max_length`, `domain` —
MUST never change once codes have been issued. Changing any of it creates a
second, unrelated permutation whose outputs may collide with already-issued
codes.

Violations of the constraints in this section MUST be rejected at codec
construction time (`ConfigError` or the language's idiomatic equivalent).

### 2.1 Key material

Users hold keys in many shapes — raw bytes, `openssl rand -hex 32` output,
base64 blobs, passphrases. All are accepted, with a deterministic rule so
every language produces the same AES key from the same input:

- **Bytes** of length exactly 16, 24, or 32 → used directly as the AES key.
- **Bytes** of any other non-zero length → derived (below).
- **String** (always, regardless of length or content — a hex-looking string
  is *not* auto-decoded, avoiding ambiguity) → its UTF-8 bytes are derived.
- Empty bytes / empty string → `ConfigError`.

Derivation: `AES-256 key = SHA-256( "dealcode/v1/kdf" ‖ material )`, where
`"dealcode/v1/kdf"` is the 15-byte ASCII prefix.

Informative: derivation is domain separation, not password stretching. A
passphrase key is exactly as strong as the passphrase; prefer ≥128-bit random
material (e.g. `openssl rand -hex 32`).

## 3. Alphabets

An alphabet is an ordered sequence of distinct characters. The character at
index `i` represents numeral value `i`. Codes are rendered and parsed
big-endian (most significant numeral first).

### 3.1 Presets

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

"ASCII-lowercase/uppercase" maps only `A–Z`/`a–z`; all other characters are
left untouched. Normalization applies to `decode` input only; `encode` always
emits the canonical characters listed above.

### 3.2 Custom alphabets

A custom alphabet is any string of **2 to 94 distinct** printable ASCII
characters (code points 0x21–0x7E, i.e. no spaces or control characters).
Custom alphabets have **no normalization**: decode input must match exactly.

Implementations SHOULD accept the `alphabet` parameter as either a preset name
or a custom alphabet string; preset names win on conflict.

## 4. Length staging

Let `r = radix`, `m = min_length`, `M = max_length`. The counter space
`[0, r^M)` is partitioned into contiguous **stages**, one per code length `d`:

- Stage `m` (the first stage) covers `n ∈ [0, r^m)` — `base(m) = 0`.
- Stage `d`, for `m < d ≤ M`, covers `n ∈ [r^(d−1), r^d)` — `base(d) = r^(d−1)`.

Equivalently: `d(n)` = the number of base-`r` digits of `n`, but never less
than `m`. The **stage value** is `v = n − base(d)`; its range size is
`capacity(d) = r^d − base(d)`.

Consequences:

- Codes have length `m` until the counter reaches `r^m`, then length `m+1`
  until `r^(m+1)`, and so on. Length growth is driven purely by exhaustion.
- Codes of different lengths trivially never collide; within one length FF1 is
  a permutation; therefore the full mapping is a bijection on `[0, r^M)`.

## 5. Encoding

Input: counter `n`. Reject `n < 0` and `n ≥ min(r^M, 2^63)`
(`RangeError` equivalent).

1. Determine stage: `d = d(n)`, `v = n − base(d)`.
2. Represent `v` as exactly `d` base-`r` numerals, big-endian, zero-padded:
   `X = STR(v, r, d)`.
3. Compute the tweak `T` = the UTF-8 bytes of the string
   `"dealcode/v1/" + domain`
   (with empty domain the tweak is exactly `dealcode/v1/`, 12 bytes).
4. `Y = FF1.Encrypt(key, T, X)` with radix `r` (§6).
5. The code is `Y` rendered through the alphabet. Its length is exactly `d`.

## 6. FF1

FF1 is implemented exactly as specified in NIST SP 800-38G ("Recommendation
for Block Cipher Modes of Operation: Methods for Format-Preserving
Encryption"), Algorithms 7 (`FF1.Encrypt`) and 8 (`FF1.Decrypt`), with AES as
the underlying block cipher. Ten rounds, alternating Feistel with the
CBC-MAC-based round function `PRF`.

Implementation notes (normative for interoperability):

- `b = ⌈⌈v·log2(r)⌉ / 8⌉` where `v` is the length of the right half. Compute
  `⌈v·log2(r)⌉` exactly as the bit length of `r^v − 1` — floating-point log
  MUST NOT be used.
- Within dealcode's configuration bounds (`r^max_length ≤ 2^128`, `r ≤ 94`):
  `r^v < 2^68`, so `b ≤ 9`, `d_len = 4⌈b/4⌉ + 4 ≤ 16`, and `y` fits in 128
  bits; the `S` expansion never needs extra AES calls. Implementations SHOULD
  nevertheless implement the general expansion loop
  (`S = R ‖ CIPH(R ⊕ [1]¹⁶) ‖ CIPH(R ⊕ [2]¹⁶) ‖ …`, truncated to `d_len`
  bytes) so the FF1 core passes the NIST sample vectors unmodified.
- Intermediate values exceed 64 bits. In languages without arbitrary
  precision, 128-bit arithmetic suffices: compute
  `c = (NUM(A) + (y mod r^m)) mod r^m` — reduce `y` first so the sum cannot
  overflow.

Every implementation MUST pass the official NIST FF1 sample vectors
(`testvectors/ff1_nist.json`, sourced from NIST's published
[FF1 examples](https://csrc.nist.gov/csrc/media/projects/cryptographic-standards-and-guidelines/documents/examples/ff1samples.pdf);
NIST publications are U.S. public domain).

AES itself MUST come from the platform's standard or widely audited
cryptographic library — do not hand-roll AES.

## 7. Decoding

Input: code string `s`.

1. Apply the alphabet's normalization (§3.1) to `s`.
2. Reject if the length is `< min_length` or `> max_length`, or if any
   character is not in the alphabet (`InvalidCodeError` equivalent).
3. `d = len(s)`; map characters to numerals `Y`.
4. `X = FF1.Decrypt(key, T, Y)` with the same tweak `T` as §5.
5. `v = NUM(X, r)`.
6. **Range check** — the code was never issued by this codec; reject
   (`InvalidCodeError`) if either:
   - `d > min_length` and `v ≥ r^d − r^(d−1)` (outside the stage), or
   - `base(d) + v ≥ 2^63` (outside the counter space; only reachable when
     `r^max_length > 2^63`).
7. Return `n = base(d) + v`.

Note: decode rejecting a string does not mean the string "looks wrong" — a
well-formed unissued code decrypts to garbage or to an out-of-range stage
value. Decode success only proves the code is *consistent* with the key; the
application still decides whether counter `n` actually exists.

## 8. Errors

Three distinguishable error kinds, using each language's idiomatic mechanism:

| Kind               | Raised when |
|--------------------|-------------|
| `ConfigError`      | invalid key size, alphabet, lengths, or domain at construction |
| `RangeError`       | `encode` called with `n < 0` or `n ≥ r^M` |
| `InvalidCodeError` | `decode` input fails length/charset/stage-range checks |

Implementations MUST NOT silently truncate, wrap, or "fix" invalid input.

## 9. Test vectors

- `testvectors/ff1_nist.json` — the 9 official NIST FF1 samples. Validates the
  FF1 core.
- `testvectors/v1.json` — dealcode format-v1 vectors across alphabets, stage
  boundaries, domains, normalization cases and invalid codes. Generated by the
  Python reference implementation (`scripts/generate_test_vectors.py`).
  Counters are encoded as **JSON strings** (they exceed 2^53).

Passing both files is the definition of conformance.

## 10. Security model (informative)

- Uniqueness does not depend on the key being secret; it follows from FF1
  being a permutation. The key protects *unpredictability*: without it, codes
  reveal nothing about issue order or volume.
- Dealcode codes are **not** authentication tokens. The code space is small
  (e.g. 16.7M for `hex`/length 6); an online attacker can guess valid codes at
  a rate proportional to `issued / capacity`. Rate-limit lookups, and use
  ≥128-bit random tokens for anything security-critical.
- FF1 on small domains has known distinguishing attacks far below AES
  security margins; for the obfuscation purpose of dealcode this is
  acceptable, but do not encrypt *confidential data* with this library.
- If the key leaks, the full issue order of all past codes is revealed.
  Treat the key like any other production secret (KMS/Vault, per-environment
  keys).
