# Rust

Rust implementation of the [spec](../spec.md). Requires Rust ≥ 1.75. The
only runtime dependencies are the audited
[RustCrypto](https://github.com/RustCrypto) crates
[`aes`](https://crates.io/crates/aes) and
[`sha2`](https://crates.io/crates/sha2).

Source of truth: [`rust/` on GitHub](https://github.com/algorix-hq/dealcode/tree/main/rust)
· [full README](https://github.com/algorix-hq/dealcode/blob/main/rust/README.md)

## Install

```sh
cargo add dealcode            # once published to crates.io
```

Not on crates.io yet — until then, use a git dependency:

```toml
[dependencies]
dealcode = { git = "https://github.com/algorix-hq/dealcode" }
```

## Minimal example

```rust
use dealcode::Dealcode;

let codec = Dealcode::new("0a1b...64-hex-chars-from-your-secret-manager")?;

codec.encode(0)?;        // "767a5b"   (6 hex chars)
codec.encode(1)?;        // "421163"   never collides with any other counter
codec.decode("421163")?; // 1
```

## API surface

| Item | Notes |
|------|-------|
| `Dealcode::new(key)` | defaults; key is `impl Into<Key>`: `&str`, `String`, `&[u8]`, `Vec<u8>`, `[u8; N]` |
| `Dealcode::builder(key).alphabet(..).min_length(..).max_length(..).domain(..).build()` | invalid config → `Err(Error::Config)` |
| `codec.encode(n: u64) -> Result<String>` | `Err(Error::Range)` outside `[0, codec.capacity())` |
| `codec.decode(code) -> Result<u64>` | `Err(Error::InvalidCode)` for malformed input |
| Errors | `dealcode::Error` (`Config` / `Range` / `InvalidCode`), implements `std::error::Error` |

A `Dealcode` instance is immutable and `Send + Sync` — share it in an `Arc`
or a `static`. AES round keys and per-length FF1 parameters are precomputed
at build time.

## Tests

```sh
cd rust && cargo test && cargo clippy --all-targets -- -D warnings
```

Covers the official NIST FF1 sample vectors, every shared cross-language
vector, behavioural cases, and doctests.
