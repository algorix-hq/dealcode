# dealcode

**Collision-free, random-looking codes from a counter** — like dealing cards
from a shuffled deck. Every card comes out exactly once; the order looks
random; the dealer only remembers how many cards have been dealt.

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
[Why dealcode exists](philosophy.md).)

```
counter:  0        1        2        3        ...      16,777,216
           │        │        │        │                  │
           ▼        ▼        ▼        ▼                  ▼
code:    d568e1   f7f229   0f868d   f37ff8   ...       7b11743    ← grew to 7 chars
                                                                    only when 6 ran out
```

Give dealcode a never-repeating integer (a database sequence, an
auto-increment id) and a secret key; it gives you a short code with four
properties:

<div class="grid cards" markdown>

- :material-cards-playing-outline: **Never collides**

    ---

    The mapping is a keyed permutation (FF1, NIST SP 800-38G), so uniqueness
    is mathematical, not probabilistic. No retry loops, no birthday problem,
    no `UNIQUE`-violation handling as a code path.

- :material-eye-off-outline: **Doesn't leak your numbers**

    ---

    Sequential inputs produce scattered, unpredictable outputs. Order volume,
    issue rate, and "how many came before me" stay private — no
    [German tank problem](https://en.wikipedia.org/wiki/German_tank_problem).

- :material-arrow-collapse-horizontal: **Stays as short as possible**

    ---

    Codes start at 6 characters (configurable) and grow by one character only
    when the current length is exhausted.

- :material-swap-horizontal: **Decodes back**

    ---

    With the key, a code maps back to its counter. Look up
    `orders WHERE id = decode(code)` — no extra column index required, and
    obviously-invalid codes are rejected before touching the database.

</div>

The same key + config produces the same mapping in every language. The
[specification](spec.md) is normative, and shared test vectors keep all seven
implementations bit-identical.

## Sixty-second tour

Python shown; [every language mirrors it](getting-started.md):

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
CyclingDealcode(key, "crockford", length=6)        # fixed forever — reuses the space per cycle (see guide)
RangeDealcode(key, low=100_000, high=999_999)      # integer codes — 6 digits, never a leading zero (see guide)
Dealcode(key, "!@#$%^&*")                          # your own alphabet, why not
```

## Seven implementations, one mapping

| Language | Directory | Install | Crypto dependency |
|----------|-----------|---------|-------------------|
| [Python](languages/python.md) | [`python/`](https://github.com/algorix-hq/dealcode/tree/main/python) | `pip install dealcode` | [`cryptography`](https://cryptography.io) (PyCA) |
| [TypeScript / JavaScript](languages/js.md) | [`js/`](https://github.com/algorix-hq/dealcode/tree/main/js) | `npm install dealcode` | `node:crypto` (built-in) |
| [Go](languages/go.md) | [`go/`](https://github.com/algorix-hq/dealcode/tree/main/go) | `go get github.com/algorix-hq/dealcode/go` | standard library |
| [Java](languages/java.md) | [`java/`](https://github.com/algorix-hq/dealcode/tree/main/java) | Maven `io.algorix:dealcode` | JCE (built-in) |
| [Rust](languages/rust.md) | [`rust/`](https://github.com/algorix-hq/dealcode/tree/main/rust) | `cargo add dealcode` | RustCrypto `aes`, `sha2` |
| [C](languages/c.md) | [`c/`](https://github.com/algorix-hq/dealcode/tree/main/c) | vendored / static lib | OpenSSL libcrypto |
| [C++](languages/cpp.md) | [`cpp/`](https://github.com/algorix-hq/dealcode/tree/main/cpp) | wraps the C core | OpenSSL libcrypto |

!!! note "Registry status"

    v1.0.1 is live on [PyPI](https://pypi.org/project/dealcode/),
    [npm](https://www.npmjs.com/package/dealcode),
    [crates.io](https://crates.io/crates/dealcode), and
    [Maven Central](https://central.sonatype.com/artifact/io.algorix/dealcode);
    `go get` resolves from GitHub directly. C and C++ are vendored by
    design (see each [language page](languages/python.md)).

Everything else is dependency-free by design: FF1 and the dealcode layer are
implemented from the NIST specification in each language and validated
against the official NIST sample vectors plus this repo's shared vectors
([`testvectors/`](https://github.com/algorix-hq/dealcode/tree/main/testvectors)).

## How it works

`encode(n)` picks the code length `d` by range (counter `< 16^6` → 6 hex
chars, `< 16^7` → 7, ...), writes `n` as a `d`-digit number, and encrypts
those digits with FF1 — format-preserving encryption that outputs *another
`d`-digit number* under your key. Same-length codes can't collide because
encryption is a bijection; different-length codes can't collide because they
have different lengths. `decode` runs it backwards and validates strictly.

Details: [Specification](spec.md) · rationale: [Design decisions](design.md)
· problem statement: [Why dealcode exists](philosophy.md).

## When to use it — and when not to

Use dealcode for order numbers, coupon and invite codes, ticket numbers,
support PINs, shortlinks: things that must be **unique, short, and
non-revealing**, where you already have (or can trivially add) a counter.

Do **not** use it for session tokens, API keys, or password-reset links — the
code space is deliberately small, so use ≥128-bit random tokens for anything
that *authenticates*. The full reasoning and an alternatives table live in
[Why dealcode exists](philosophy.md); the threat model is spelled out in the
[security model](guide/security.md).

!!! danger "One rule to remember"

    Key, alphabet, lengths, and domain are **frozen the moment the first code
    ships**. Changing any of them for an existing namespace can collide with
    already-issued codes. New scheme → new domain (or new key + new
    namespace).

## For AI coding agents

Using an AI assistant to write code against dealcode? Give it the docs in
agent-readable form — [`llms.txt`](https://algorix-hq.github.io/dealcode/llms.txt)
(index) and
[`llms-full.txt`](https://algorix-hq.github.io/dealcode/llms-full.txt)
(the entire documentation, spec included, as one file) — and install the
usage-rules skill so it knows the operational invariants (frozen config,
decode semantics, cycling mode):

```sh
npx skills add algorix-hq/dealcode
```

## License

[MIT](https://github.com/algorix-hq/dealcode/blob/main/LICENSE) © Algorix
Corporation.
