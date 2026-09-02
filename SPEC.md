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
| `min_length` | integer | `6`            | `2 ≤ min_length ≤ 128` and `radix^min_length ≥ 100` |
| `max_length` | integer | largest `L` with `radix^L ≤ 2^63 − 1` | `min_length ≤ max_length ≤ 128` and `radix^max_length ≤ 2^128` |
| `domain`     | string  | `""`           | valid Unicode (no U+0000, no unpaired surrogates); UTF-8 byte length ≤ 255 |

The explicit `≤ 128` length bound is implied by `radix ≥ 2` and
`radix^max_length ≤ 2^128`, but implementations MUST check it **before**
computing any power so that absurd inputs are rejected in O(1) rather than
after unbounded big-integer work.

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
- **Bytes** of any other non-zero length → derived (below). Byte content is
  unrestricted.
- **String** (always, regardless of length or content — a hex-looking string
  is *not* auto-decoded, avoiding ambiguity) → its UTF-8 bytes are derived.
  String key material MUST be valid Unicode: implementations MUST reject
  U+0000 and unpaired surrogates (`ConfigError`) rather than silently
  replacing, truncating, or re-encoding them — the same rule applies to
  `domain`. (Rationale: languages disagree on how to smuggle such strings
  into UTF-8, so accepting them would silently produce different permutations
  per language; and NUL-terminated C APIs cannot represent them at all.)
- Empty bytes / empty string → `ConfigError`.
- **String equal to a preset alphabet name** (ASCII case-insensitively:
  `dec`, `hex`, `base32`, `crockford`, `base36`, `base58`, `base62`,
  `base64url`) → `ConfigError`. Such a "key" is almost certainly a swapped
  argument (`Dealcode("crockford")` where `Dealcode(key, "crockford")` was
  meant), and no real key material collides with this tiny set. Byte keys are
  unaffected.

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

Informative: `crockford` normalization intentionally covers **only** case and
the `O/I/L` confusables. Unlike Crockford's Base32 essay, separators are NOT
ignored: a hyphenated or whitespace-grouped rendering (`H4P-FG6`) must have
its separators stripped by the application before `decode`. No preset trims
or Unicode-normalizes input.

### 3.2 Custom alphabets

A custom alphabet is any string of **2 to 94 distinct** printable ASCII
characters (code points 0x21–0x7E, i.e. no spaces or control characters).
Custom alphabets have **no normalization**: decode input must match exactly.

Implementations SHOULD accept the `alphabet` parameter as either a preset name
or a custom alphabet string; preset names win on conflict.

A custom alphabet that is not exactly a preset name but ASCII-case-
insensitively equals one (`"HEX"`, `"Base62"`, …) MUST be rejected with
`ConfigError`. Accepting it would silently build a codec over the *letters of
the name* (`{H,E,X}` as radix 3) — a plausible-looking misconfiguration that
is frozen into production the moment the first code ships. A genuinely
intended alphabet of those exact characters can be expressed by reordering it.

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

FF1 is implemented exactly as specified in
[NIST SP 800-38G](https://csrc.nist.gov/pubs/sp/800/38/g/final)
("Recommendation for Block Cipher Modes of Operation: Methods for
Format-Preserving Encryption", March 2016), Algorithms 7 (`FF1.Encrypt`) and
8 (`FF1.Decrypt`), with AES as the underlying block cipher. Ten rounds,
alternating Feistel with the CBC-MAC-based round function `PRF`. (The Rev. 1
draft changes recommendations, not these algorithms; conformance targets the
algorithms as published in 2016.)

Notation caution: this section reuses NIST's own symbols, which collide with
§4–§5 — here `v` is the length of the **right half** of the numeral string
(not the stage value) and `m` is the per-round half length (not
`min_length`).

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

1. Reject if the length is `< min_length` or `> max_length`
   (`InvalidCodeError` equivalent). Checking length first keeps rejection of
   oversized garbage cheap (no normalized copy is ever allocated); it is
   observationally identical to normalizing first, because normalization
   (§3.1) is length-preserving.
2. Apply the alphabet's normalization (§3.1) to `s`. Reject if any
   character is not in the alphabet (`InvalidCodeError`).
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
| `RangeError`       | `encode` called with `n < 0` or `n ≥ min(r^M, 2^63)` (§5) |
| `InvalidCodeError` | `decode` input fails length/charset/stage-range checks |

Implementations MUST NOT silently truncate, wrap, or "fix" invalid input.

## 9. Test vectors

- `testvectors/ff1_nist.json` — the 9 official NIST FF1 samples. Validates the
  FF1 core.
- `testvectors/v1.json` — dealcode format-v1 vectors across alphabets, stage
  boundaries, domains, normalization cases and invalid codes. Generated by the
  Python reference implementation (`scripts/generate_test_vectors.py`).
  Counters are encoded as **JSON strings** (they exceed 2^53).

For each config in `v1.json` a conforming implementation must: produce
`code` for every `vectors[].n` and decode it back; reject every
`invalid_codes[]` entry (`InvalidCodeError`); accept every `normalize[]`
input as its `n`; and reject every `range_counters[]` value (`RangeError`) —
a value unrepresentable in the language's counter type (e.g. `-1` or `2^64`
for `uint64`) counts as rejected by the type system. Every entry of the
top-level `invalid_configs[]` must fail construction (`ConfigError`).

Passing both files is the definition of conformance for the core codec.
Additionally, `testvectors/v1c.json` covers the fixed-length cycling mode
(§11) and `testvectors/v1r.json` the integer range mode (§12) — each
required for implementations that ship the respective mode (§11.4, §12.5);
all seven in this repository ship both.

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
- If the key leaks, the full issue order of all past codes is revealed, and
  every *future* valid code becomes enumerable (encode every counter).
  Treat the key like any other production secret (KMS/Vault, per-environment
  keys).

## 11. Fixed-length cycling mode (v1c)

An additive mode for code shapes that must **never grow** — airline-PNR-style
fixed-length codes. The counter space is used in **cycles**: each cycle fills
the entire fixed-length code space exactly once, and when it is exhausted the
next cycle refills the *same* space through a **different permutation**
(a different FF1 tweak), so reuse does not replay the previous order.

This mode lives in its own tweak namespace and changes nothing about §1–§10:
plain-v1 tweaks always start with the 12 bytes `dealcode/v1/`, cycling
tweaks with the 13 bytes `dealcode/v1c/` — the byte at offset 11 (`/` vs
`c`) makes the two sets disjoint for every possible domain and cycle.

### 11.1 Configuration

A cycling codec is defined by `key` (§2.1, same rules and the same
preset-name guard), `alphabet` (§3, same rules and guards), a single fixed
`length` `L` (default `6`), and `domain` (same rules as §2). Constraints,
all `ConfigError` at construction:

- `2 ≤ L ≤ 128` (checked before any power is computed, so absurd lengths
  are rejected in O(1)) and `radix^L ≥ 100` (FF1 structural minimum);
- `radix^L ≤ 2^63` — the per-cycle capacity `C = radix^L` must itself fit
  the counter space, otherwise a cycle could never complete and plain v1
  with `min_length = max_length = L` is the right tool.

The **counter space is unchanged**: `0 ≤ n < 2^63`. Derived values:
`cycle(n) = ⌊n / C⌋` and `v(n) = n mod C`. The largest usable cycle is
`max_cycle = ⌊(2^63 − 1) / C⌋`.

### 11.2 Encoding and decoding

Encode (input counter `n`; reject `n < 0` and `n ≥ 2^63` with `RangeError`):

1. `e = cycle(n)`, `v = v(n)`, `X = STR(v, r, L)` (§5 step 2).
2. Tweak `T_e` = the UTF-8 bytes of
   `"dealcode/v1c/" + decimal(e) + "/" + domain`, where `decimal(e)` is the
   base-10 rendering of `e` with no leading zeros (`"0"` for cycle zero,
   never `"00"`). With `domain` ≤ 255 bytes and `e ≤ max_cycle` the tweak
   is at most 288 bytes.
3. The code is `FF1.Encrypt(key, T_e, X)` (§6) rendered through the
   alphabet — always exactly `L` characters.

Decode takes the code **and the cycle number** `e` (a code alone is
ambiguous by design — see §11.3):

1. Reject `e < 0` or `e > max_cycle` (`RangeError`).
2. Apply §7 with `min_length = max_length = L` and tweak `T_e`; the stage
   range check reduces to `v < C`, which always holds, and the counter
   bound check is `e·C + v < 2^63`, which can only fail in the final
   partial cycle.
3. Return `n = e·C + v`.

Normalization (§3.1) applies exactly as in plain v1.

### 11.3 Semantics (normative for applications)

- **Within one cycle** codes are unique (FF1 is a permutation of the
  fixed-length space) — a cycle issues each of the `C` possible strings
  exactly once, in a key-and-cycle-dependent order.
- **Across cycles the same strings recur** (pigeonhole: the space is being
  refilled). Cycling mode is therefore only sound when at most one cycle's
  codes are *live* at a time in a given uniqueness scope: retire or expire
  cycle `e`'s codes before issuing from cycle `e+1`, or scope storage by
  cycle. A global `UNIQUE(code)` index spanning cycles WILL fire — scope it
  as `UNIQUE(cycle, code)` or equivalent.
- The application must persist which cycle each live code belongs to (or
  equivalently, the currently active cycle) to decode; the library cannot
  recover `e` from the code string.

### 11.4 Test vectors

`testvectors/v1c.json` (generated by the same
`scripts/generate_test_vectors.py`) covers cycling-mode configs: for each,
`vectors[]` entries are `{n, code}` with `cycle(n)` implied by `n`;
conforming implementations must produce `code` for every `n`, decode it back
under `cycle(n)`, reject every `invalid_codes[]` entry for the cycle it
names (`InvalidCodeError`), accept every `normalize[]` input as its `n`
under its cycle, reject every `range_counters[]` value (`RangeError`),
reject every `invalid_cycles[]` value when passed as the cycle to decode
(`RangeError`), and fail construction for every `invalid_configs[]` entry
(`ConfigError`). Passing it is required for conformance of any
implementation that ships the mode, and all seven in this repository do.

## 12. Integer range mode (v1r)

An additive mode for codes that must be **integers in an application-chosen
range** — e.g. 6-digit numbers with no leading zero (`[100000, 999999]`),
which survive round-trips through integer columns, spreadsheets, and any
system that would strip a leading zero from a string code. Codes in this
mode are integers, not alphabet strings; §3–§5 and §7 do not apply.

The mode is deliberately **walk-free**: encode and decode are always exactly
one FF1 call. FF1 domains are `radix^m`, and an arbitrary range size `N` is
generally not such a power, so instead of forcing the exact range with
cycle-walking (re-encrypting until the value lands inside — unbounded worst
case, and a per-language loop whose semantics must never drift), the mode
uses the **largest FF1 domain that fits inside the range** and issues codes
from `low` upward through it. The trade is a small, exactly-computable slice
of the range that is never issued (< 4 % for `N ≥ 10^5`; zero when `N`
itself is an admissible power) in exchange for a constant-time, loop-free
mapping.

Like cycling mode, this mode lives in its own tweak namespace and changes
nothing about §1–§11: the byte at offset 11 is `/` for v1, `c` for v1c and
`r` for v1r, so the three tweak sets are pairwise disjoint for every
possible configuration.

### 12.1 Configuration

A range codec is defined by `key` (§2.1, same rules and the same
preset-name guard), integers `low` and `high`, and `domain` (same rules as
§2). Constraints, all `ConfigError` at construction:

- `0 ≤ low ≤ high ≤ 2^63 − 1`;
- `N = high − low + 1 ≥ 100` (FF1 structural minimum, §2).

### 12.2 Domain selection (normative)

The internal FF1 domain is the largest `radix^m ≤ N` with
`2 ≤ radix ≤ 256` and `2 ≤ m ≤ 63`; among equal capacities the smallest
`m` wins. Deterministically:

```
best_capacity = 0; best = (radix, m) = ⊥
for m in 2 .. 63:
    r = min(iroot(N, m), 256)     # iroot = exact integer m-th root
    if r < 2: continue
    c = r^m                        # c ≤ N by construction
    if c > best_capacity:          # strict '>' ⇒ smallest m on ties
        best_capacity = c; best = (r, m)
radix, m = best; capacity = best_capacity
```

`iroot(N, m)` is the largest integer `r` with `r^m ≤ N` and MUST be
computed with exact integer arithmetic (binary search with
overflow-checked powers, or equivalent) — floating-point roots MUST NOT
be used. The loop always finds a candidate (`m = 2` gives
`r = min(⌊√N⌋, 256) ≥ 10` for `N ≥ 100`, so `capacity ≥ 100`).

The `radix ≤ 256` bound is structural: it keeps every numeral in one byte,
matching the numeral representation all seven FF1 cores were validated
with. It costs little — with radix up to 256 available, consecutive
admissible powers are dense (`(r+1)^m / r^m ≈ 1 + m/r`), giving
`capacity / N > 96 %` for all `N ≥ 10^5` and exactly `N` whenever `N` is
an admissible power (examples: `N = 10^6` → `100^3`, capacity = N;
`N = 2^63` → `128^9`, capacity = N; `N = 900 000` → `96^3 = 884 736`,
98.3 %).

Derived values (all fixed at construction): `capacity`, and the issued
code range `[low, low + capacity − 1]`. Values in
`[low + capacity, high]` — the **dead zone** — are never issued and are
rejected by decode. Applications needing the top of the range exactly
SHOULD widen `high` so that the capacity covers what they need.

### 12.3 Encoding and decoding

Counters are `0 ≤ n < capacity` (`RangeError` outside — this mode has no
staging and no cycles; when the range is exhausted, it is exhausted).

Encode:

1. `X = STR(n, radix, m)` — `n` as `m` big-endian base-`radix` numerals.
2. Tweak `T` = the UTF-8 bytes of
   `"dealcode/v1r/" + decimal(low) + "/" + decimal(high) + "/" + domain`,
   where `decimal(·)` is the base-10 rendering with no leading zeros
   (`"0"` for zero). With `domain` ≤ 255 bytes the tweak is at most 310
   bytes. Binding `low` and `high` into the tweak makes different ranges
   unrelated permutations, exactly as `domain` does.
3. `Y = FF1.Encrypt(key, T, X)` (§6); the code is the integer
   `low + NUM(Y, radix)`.

Decode (input: integer code `c`):

1. Reject `c < low` or `c > high` (`InvalidCodeError`).
2. Reject `c ≥ low + capacity` (dead zone, `InvalidCodeError`).
3. `Y = STR(c − low, radix, m)`; `n = NUM(FF1.Decrypt(key, T, Y), radix)`.
   `n < capacity` always holds (FF1 permutes `[0, radix^m)`), so no
   further range check is needed. Return `n`.

Codes are integers end to end; implementations MUST NOT accept or produce
string codes in this mode (rendering is the application's concern, and
`decimal(code)` never has a leading zero when `low ≥ 10^(k−1)`).

### 12.4 Semantics (normative for applications)

- Uniqueness is structural, as in plain v1: distinct counters in
  `[0, capacity)` give distinct codes in `[low, low + capacity)`.
- The **effective capacity is `capacity`, not `N`** — surface it
  (implementations MUST expose it) and monitor counter consumption
  against it.
- The security model is §10 unchanged: same FF1 core, one call per
  operation; adding the public constant `low` is a fixed bijection and
  affects nothing. An observer may notice that codes never exceed
  `low + capacity − 1`; that reveals only the (public) scheme parameters,
  never counter order or volume.
- The immutability rule (§2) applies to `key`, `low`, `high`, and
  `domain`.

### 12.5 Test vectors

`testvectors/v1r.json` (generated by the same
`scripts/generate_test_vectors.py`) covers range-mode configs, each
recording the expected derived `radix`, `m`, and `capacity` (a conforming
implementation must derive the same values). For each config:
`vectors[]` entries are `{n, code}` (both JSON strings) — implementations
must produce `code` for every `n` and decode it back; reject every
`invalid_codes[]` integer (`InvalidCodeError` — below `low`, above
`high`, or in the dead zone); reject every `range_counters[]` value
(`RangeError`); and fail construction for every top-level
`invalid_configs[]` entry (`ConfigError`). Passing it is required for
conformance of any implementation that ships the mode, and all seven in
this repository do.
