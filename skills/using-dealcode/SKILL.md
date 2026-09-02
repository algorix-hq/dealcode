---
name: using-dealcode
description: >-
  Guides correct use of the dealcode library, which turns database counters
  into short collision-free random-looking codes (FF1, NIST SP 800-38G) and
  back. Use when writing, reviewing, or debugging code that issues or parses
  order numbers, coupon codes, invite codes, ticket numbers, booking/PNR
  codes, or short IDs with dealcode in Python, TypeScript/JavaScript, Go,
  Java, Rust, C, or C++ — including choosing a configuration, wiring it to a
  database, handling decode errors, the fixed-length cycling mode, and the
  integer range mode.
license: MIT
metadata:
  source: https://github.com/algorix-hq/dealcode
---

# Using dealcode

dealcode maps a never-repeating integer counter to a short random-looking
code and back. Same key + config → byte-identical codes in every language,
so services in different languages can share one namespace. It is NOT
encryption for secrecy, and codes are NOT auth tokens.

Default shape — one codec per namespace, built once at startup, reused
(immutable, thread-safe):

```python
codec = Dealcode(key, "crockford", domain="orders")
code = codec.encode(n)      # n from a DB sequence
n = codec.decode(code)      # then CHECK n EXISTS in the DB
```

Per-language constructors, DB recipes, and good/bad code pairs:
see [references/patterns.md](references/patterns.md).
Complete docs in one fetch:
<https://algorix-hq.github.io/dealcode/llms-full.txt>

## Rules (violations are production incidents)

1. **The config is frozen once the first code ships.** Key, alphabet,
   min/max length, domain — never change any of them for a live namespace.
   New scheme → new domain (or new key + new namespace).
2. **Never hex-decode a string key.** Bytes of exactly 16/24/32 are used
   directly; ANY string (hex-looking included) is derived from its UTF-8
   bytes. Pick one form per namespace forever — the two forms produce
   different codes.
3. **decode is parsing, not proof of existence.** Every well-formed code
   decodes to some counter. Always follow decode with an existence lookup;
   rate-limit public decode endpoints.
4. **decode never trims.** Strip whitespace and display separators
   (`XXXX-XXXX` hyphens) before decoding. Built-in normalization only fixes
   letter case and crockford's `O→0`, `I/L→1`.
5. **Counter is the primary key; `UNIQUE(code)` is a tripwire.** If that
   index ever fires, the config changed mid-namespace — investigate, never
   retry with a new counter.
6. **Cycling mode reuses codes by design.** With the fixed-length cycling
   codec, persist the cycle with each code, index `UNIQUE(cycle, code)`,
   and retire cycle `e` before issuing from `e+1`. `decode(code, cycle)`
   requires the stored cycle; a wrong in-range cycle silently returns a
   different counter — only the existence lookup catches it.
7. **Range mode capacity is `codec.capacity`, not `high − low + 1`.** The
   integer range mode (`RangeDealcode(key, low=100_000, high=999_999)`)
   issues codes as *integers* (int-column safe, no leading zero) via the
   largest FF1 domain fitting the range — for 100000–999999 that is
   884,736 of 900,000. Counters ≥ capacity are rejected; the unissued top
   slice of the range fails decode. No cycles: plain `UNIQUE(code)`
   tripwire semantics apply.
8. **Never reimplement the algorithm.** Conformance is defined by shared
   test vectors; use the library in all seven languages.

## When a call is rejected

- Config error mentioning a preset name → arguments are swapped
  (`Dealcode("crockford")`) or a preset is miscased (`"HEX"`); preset names
  are lowercase.
- Range error on encode → counter is negative or ≥ capacity
  (`min(radix^max_length, 2^63)`).
- Invalid-code error on decode → check for untrimmed separators/whitespace
  first; otherwise the code has the wrong length/charset or was never
  issued by this codec.

Normative details: the
[specification](https://raw.githubusercontent.com/algorix-hq/dealcode/main/SPEC.md).
