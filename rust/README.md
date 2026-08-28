# dealcode (Rust)

Collision-free, random-looking codes from a counter. Rust implementation of
the [dealcode spec](https://github.com/algorix-hq/dealcode/blob/main/SPEC.md).

## Install

```sh
cargo add dealcode
```

Not yet published to crates.io — until then, use it as a git dependency:
`dealcode = { git = "https://github.com/algorix-hq/dealcode" }` (Cargo finds
the crate inside the repository by its package name; no path hint needed).

Requires Rust ≥ 1.75. The only runtime dependencies are the audited
[RustCrypto](https://github.com/RustCrypto) crates
[`aes`](https://crates.io/crates/aes) and
[`sha2`](https://crates.io/crates/sha2), used for AES and key derivation.

## Quickstart

```rust
use dealcode::Dealcode;

let codec = Dealcode::new("0a1b...64-hex-chars-from-your-secret-manager")?;

codec.encode(0)?;        // "767a5b"   (6 hex chars)
codec.encode(1)?;        // "421163"   never collides with any other counter
codec.decode("421163")?; // 1
# Ok::<(), dealcode::Error>(())
```

The key can be raw bytes (16/24/32 bytes are used as-is as an AES key) or any
string/bytes, which are deterministically expanded to an AES-256 key. Generate
one with `openssl rand -hex 32` and keep it in your secret manager — the
mapping is stable only while the key (and every other option) stays fixed.

### Options

```rust
use dealcode::Dealcode;

let codec = Dealcode::builder(key)  // impl Into<Key>: &str, String, &[u8], Vec<u8>, [u8; N]
    .alphabet("hex")     // "dec" | "hex" | "base32" | "crockford" | "base36"
                         // | "base58" | "base62" | "base64url" | custom string
    .min_length(6)       // codes start at this length...
    .max_length(15)      // ...and grow one char at a time up to this
                         // (default: max length whose code space fits 2^63)
    .domain("")          // namespace: same key, unrelated codes per domain
    .build()?;
```

```rust
let coupon = Dealcode::builder(key).alphabet("crockford").domain("coupons").build()?;
// human-friendly, e.g. "97HVZ6"
let order = Dealcode::builder(key).alphabet("dec").min_length(8).domain("orders").build()?;
// digits only
let fixed = Dealcode::builder(key).min_length(16).max_length(16).build()?;
// constant-length hex
```

`decode` returns `Err(Error::InvalidCode(_))` for **malformed** input — wrong
length, characters outside the alphabet, or a value outside the issuable
range. A *well-formed* code always decodes to some counter, whether or not
that counter was ever issued (inherent to a permutation — see SPEC §7).
Treat decode as parsing, not proof of existence: look the counter up before
acting on it, and note that a one-character typo in a valid code can resolve
to a *different* valid counter — add rate limiting (and, for human-typed
flows, an existence check or your own check digit).
`encode` returns `Err(Error::Range { .. })` outside `[0, codec.capacity())`.
Invalid configuration is rejected at build time with `Err(Error::Config)`.
All three are variants of `dealcode::Error`, which implements
`std::error::Error`.

## Using it with your database

Dealcode does not talk to your database — it only turns a counter into a code.
Any source of never-repeating integers works. With PostgreSQL:

```sql
CREATE SEQUENCE order_code_seq AS bigint MINVALUE 0 START WITH 0;

CREATE TABLE orders (
  id   bigint PRIMARY KEY,          -- the counter
  code text NOT NULL UNIQUE,        -- safety net; alerts on config mistakes
  ...
);
```

```rust
use dealcode::{Dealcode, Error};

let codec = Dealcode::builder(std::env::var("DEALCODE_KEY").unwrap())
    .domain("orders")
    .build()?;

// on create:
let n: i64 = client.query_one("SELECT nextval('order_code_seq')", &[])?.get(0);
let code = codec.encode(n as u64)?;
client.execute("INSERT INTO orders (id, code) VALUES ($1, $2)", &[&n, &code])?;

// on lookup:
fn find_order(client: &mut Client, codec: &Dealcode, code: &str) -> Option<Row> {
    let n = codec.decode(code).ok()?;   // malformed codes never reach the DB
    client.query_opt("SELECT * FROM orders WHERE id = $1", &[&(n as i64)]).ok()?
}
```

Sequences never hand out the same number twice (even across concurrent
transactions and rollbacks), so codes never collide. Gaps in the sequence are
invisible — codes look random anyway.

If the `UNIQUE` constraint on `code` ever fires, do not retry: it means the
key/config changed for an existing namespace. Investigate.

## Thread safety & performance

A `Dealcode` instance is immutable and `Send + Sync`; create one per namespace
at startup and share it freely (e.g. in an `Arc` or a `static`). AES round
keys and per-length FF1 parameters are precomputed at build time; encoding is
ten AES-CBC-MAC rounds — a few microseconds, O(1) in the counter value, no
locks.

## Interoperability

This crate implements format version 1 of the spec exactly and passes the
official NIST FF1-AES sample vectors plus the shared
[`testvectors/`](https://github.com/algorix-hq/dealcode/tree/main/testvectors)
suite; codes are byte-for-byte identical to those produced by every other
conforming implementation (e.g. the
[Python package](https://github.com/algorix-hq/dealcode/tree/main/python)).

## Running the tests

From `rust/`:

```sh
cargo test
cargo clippy --all-targets -- -D warnings
```

The suite covers the official NIST FF1 sample vectors, every shared
cross-language vector in
[`testvectors/`](https://github.com/algorix-hq/dealcode/tree/main/testvectors),
behavioural cases, and doctests.

## License

MIT — see [LICENSE](https://github.com/algorix-hq/dealcode/blob/main/LICENSE).
