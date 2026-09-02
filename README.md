# dealcode

**English** | [한국어](README.ko.md) · **Docs:** <https://algorix-hq.github.io/dealcode/>

[![ci](https://github.com/algorix-hq/dealcode/actions/workflows/ci.yml/badge.svg)](https://github.com/algorix-hq/dealcode/actions/workflows/ci.yml)
[![docs](https://github.com/algorix-hq/dealcode/actions/workflows/docs.yml/badge.svg)](https://algorix-hq.github.io/dealcode/)
[![implementations](https://img.shields.io/badge/implementations-7_%C2%B7_bit--identical-1f6feb)](#implementations)
[![PyPI](https://img.shields.io/pypi/v/dealcode)](https://pypi.org/project/dealcode/)
[![npm](https://img.shields.io/npm/v/dealcode)](https://www.npmjs.com/package/dealcode)
[![crates.io](https://img.shields.io/crates/v/dealcode)](https://crates.io/crates/dealcode)
[![Maven Central](https://img.shields.io/maven-central/v/io.algorix/dealcode)](https://central.sonatype.com/artifact/io.algorix/dealcode)
[![Go Reference](https://pkg.go.dev/badge/github.com/algorix-hq/dealcode/go.svg)](https://pkg.go.dev/github.com/algorix-hq/dealcode/go)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

Collision-free, random-looking codes from a counter — like dealing cards from
a shuffled deck. Every card comes out exactly once; the order looks random;
the dealer only remembers how many cards have been dealt.

Anyone who has shipped short public codes — an airline-style booking
reference, an order number, a `cus_xxxxxx` shortcode — knows the trap:

- **Random?** The birthday problem bites absurdly early: draw random 6-digit
  codes and the first duplicate is *expected* around code **#1,200** — in a
  space of a million. From then on, every insert carries a uniqueness check
  and a retry loop.
- **UUID?** Never collides, but 36 characters — not something you print on a
  boarding pass.
- **nanoid?** Shorter, yet still long — it has to be, *because* it is random.
  Shrink it and the birthday problem comes straight back.
- **A raw sequence?** Short and collision-free — and it broadcasts exactly
  how many orders you have.

dealcode is the missing option: keep the sequence your database already
produces, and it **packs the code space full** — every code dealt exactly
once, no repeats until all 1,000,000 codes (then all 10,000,000, …) are
actually used — while the order stays cryptographically unpredictable from
outside. All you need is a counter. (Full argument and alternatives table:
[Why dealcode exists](https://algorix-hq.github.io/dealcode/philosophy/).)

```
counter:  0        1        2        3        ...      16,777,216
           │        │        │        │                  │
           ▼        ▼        ▼        ▼                  ▼
code:    d568e1   f7f229   0f868d   f37ff8   ...       7b11743    ← grew to 7 chars
                                                                    only when 6 ran out
```

Give dealcode a never-repeating integer (a database sequence, an
auto-increment id) and a secret key; it gives you a short code that

- **never collides** — the mapping is a keyed permutation (FF1, NIST
  SP 800-38G), so uniqueness is mathematical, not probabilistic. No retry
  loops, no birthday problem, no `UNIQUE`-violation handling as a code path.
- **doesn't leak your numbers** — sequential inputs produce scattered,
  unpredictable outputs. Order volume, issue rate, and "how many came before
  me" stay private (no [German tank problem](https://en.wikipedia.org/wiki/German_tank_problem)).
- **stays as short as possible** — codes start at 6 characters (configurable)
  and grow by one character only when the current length is exhausted.
- **decodes back** — with the key, a code maps back to its counter. Look up
  `orders WHERE id = decode(code)`; no extra column index required, and
  obviously-invalid codes are rejected before touching the database.

The same key + config produces the same mapping in every language.
`SPEC.md` is normative and shared test vectors keep all implementations
bit-identical.

## Implementations

| Language | Directory | Install | Crypto dependency |
|----------|-----------|---------|-------------------|
| Python   | [`python/`](python/) | `pip install dealcode` | [`cryptography`](https://cryptography.io) (PyCA) |
| TypeScript / JavaScript | [`js/`](js/) | `npm install dealcode` | `node:crypto` (built-in) |
| Go       | [`go/`](go/) | `go get github.com/algorix-hq/dealcode/go` | standard library |
| Java     | [`java/`](java/) | Maven `io.algorix:dealcode` | JCE (built-in) |
| Rust     | [`rust/`](rust/) | `cargo add dealcode` | RustCrypto `aes`, `sha2` |
| C        | [`c/`](c/) | `make install` / vendored (GCC/Clang: needs `__int128`) | OpenSSL libcrypto |
| C++      | [`cpp/`](cpp/) | CMake (wraps the C core) | OpenSSL libcrypto |

> v1.0.1 is live everywhere: [PyPI](https://pypi.org/project/dealcode/),
> [npm](https://www.npmjs.com/package/dealcode),
> [crates.io](https://crates.io/crates/dealcode),
> [Maven Central](https://central.sonatype.com/artifact/io.algorix/dealcode);
> Go modules resolve from this repository.

Everything else is dependency-free by design: FF1 and the dealcode layer are
implemented from the NIST specification in each language and validated against
the official NIST sample vectors plus this repo's shared vectors
([`testvectors/`](testvectors/)).

## Sixty-second tour (Python shown; every language mirrors it)

```python
from dealcode import Dealcode

codec = Dealcode(key="use `openssl rand -hex 32` in production")

codec.encode(0)          # 'd568e1'
codec.encode(1)          # 'f7f229'
codec.decode("f7f229")   # 1
```

Pick the shape your product needs:

```python
Dealcode(key, "crockford", domain="coupons")       # e.g. 'ZV6NQ0' — human-friendly, confusables handled
Dealcode(key, "dec",       domain="orders")        # e.g. '839207' — digits only
Dealcode(key, "base62",    min_length=8)           # e.g. 'tHx93bQk'
Dealcode(key, "hex", min_length=16, max_length=16) # fixed-length tokens
RangeDealcode(key, low=100_000, high=999_999)      # integer codes — 6 digits, never a leading zero
Dealcode(key, "!@#$%^&*")                          # your own alphabet, why not
```

- **`alphabet`** — `dec`, `hex`, `base32`, `crockford`, `base36`, `base58`,
  `base62`, `base64url`, or any 2–94 distinct printable ASCII characters.
  Presets come with sensible decode normalization (hex is case-insensitive;
  Crockford maps `O→0`, `I/L→1`).
- **`domain`** — namespaces. One key, unrelated code streams for `"orders"`,
  `"coupons"`, `"invites"`.
- **`min_length` / `max_length`** — starting and maximum code length.
  Equal values give fixed-length codes. Need fixed-length codes that *never*
  grow, PNR-style? The cycling mode (`CyclingDealcode`, SPEC §11) refills
  the same space through a different permutation each cycle instead of
  adding a character — codes repeat across cycles, so scope uniqueness per
  cycle.
- **Integer codes in a chosen range?** The range mode (`RangeDealcode`,
  SPEC §12) issues codes as *integers* from `[low, high]` — e.g.
  `low=100_000, high=999_999` for 6-digit codes with no leading zero, safe
  in an `INT` column. Always exactly one FF1 call (no cycle-walking, no
  retries); capacity is the largest FF1 domain fitting the range
  (884,736 of the 900,000 values here — 98.3%).
- **`key`** — raw AES key bytes (16/24/32) or *any* string/bytes
  (hex from `openssl rand -hex 32`, a passphrase, a KMS blob); non-AES-sized
  material is deterministically expanded, identically in every language.

## Wiring it to a database

dealcode is deliberately storage-agnostic: it needs a never-repeating integer,
which your database already knows how to produce.

```sql
CREATE SEQUENCE order_code_seq AS bigint MINVALUE 0 START WITH 0;
```

```python
n = db.scalar("SELECT nextval('order_code_seq')")   # no locks, gap-friendly
code = codec.encode(n)                              # pure computation
db.execute("INSERT INTO orders (id, code, ...) VALUES (%s, %s, ...)", (n, code))
```

Sequences guarantee no reuse even across rollbacks (gaps are invisible —
codes look random anyway), and FF1 guarantees distinct inputs give distinct
outputs. A `UNIQUE` index on `code` is a tripwire, not a mechanism: if it ever
fires, someone changed the key or config mid-namespace — investigate, don't
retry. Per-language READMEs show the same recipe idiomatically; MySQL and
others work with `AUTO_INCREMENT`/identity columns the same way.

One practical note: `decode` never trims — a copy-pasted code with a stray
space or newline is rejected as invalid. `strip()`/`trim()` user input
before decoding.

## When to use it — and when not to

Use dealcode for order numbers, coupon and invite codes, ticket numbers,
support PINs, shortlinks: things that must be **unique, short, and
non-revealing**, where you already have (or can trivially add) a counter.

Do **not** use it for session tokens, API keys, or password-reset links — the
code space is deliberately small, so use ≥128-bit random tokens for anything
that *authenticates*. Need coordination-free IDs across machines, sortable
IDs, or OTPs? Use UUIDv7/ULID/Snowflake, or HOTP/TOTP respectively. The full
reasoning, alternatives table, and operational guidance live in
[docs/philosophy.md](docs/philosophy.md).

**One rule to remember:** key, alphabet, lengths, and domain are frozen the
moment the first code ships. Changing any of them for an existing namespace
can collide with already-issued codes. New scheme → new domain (or new key +
new namespace).

## How it works

`encode(n)` picks the code length `d` by range (counter `< 16^6` → 6 hex
chars, `< 16^7` → 7, ...), writes `n` as a `d`-digit number, and encrypts
those digits with FF1 — format-preserving encryption that outputs *another
`d`-digit number* under your key. Same-length codes can't collide because
encryption is a bijection; different-length codes can't collide because they
have different lengths. `decode` runs it backwards and validates strictly.
Details: [SPEC.md](SPEC.md) · rationale: [docs/design.md](docs/design.md).

## For AI coding agents

Using an AI assistant to write code against dealcode? Install the usage
skill so it knows the operational rules (frozen config, decode semantics,
cycling mode):

```sh
npx skills add algorix-hq/dealcode
```

And point it at the docs in agent-readable form:
[`llms.txt`](https://algorix-hq.github.io/dealcode/llms.txt) (index) ·
[`llms-full.txt`](https://algorix-hq.github.io/dealcode/llms-full.txt)
(everything in one file).

## Repository layout

```
SPEC.md            the normative spec (format v1) — implementations are written from this
testvectors/       NIST FF1 samples + generated cross-language vectors; passing = conformance
python/ js/ go/ java/ rust/ c/ cpp/    independent, idiomatically packaged implementations
docs/              philosophy, design rationale (EN/KO)
scripts/           test-vector generator (runs against the Python reference)
```

Contributions and new language ports welcome — a port is conformant when it
passes both vector files. Spec changes require regenerating vectors and a
format-version bump. See [CONTRIBUTING.md](CONTRIBUTING.md); please report
suspected vulnerabilities privately per [SECURITY.md](SECURITY.md), and be
excellent to each other per the [Code of Conduct](CODE_OF_CONDUCT.md).

## License

[MIT](LICENSE) © Algorix Corporation
