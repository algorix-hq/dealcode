//! Collision-free, random-looking codes from a counter.
//!
//! Dealcode maps a non-negative integer counter `n` (from a database sequence
//! or any other source that never repeats) to a short, fixed-alphabet,
//! random-looking string called a *code*, and back. The mapping is a keyed
//! bijection (FF1 format-preserving encryption per NIST SP 800-38G), so two
//! different counters can never produce the same code, and a code decodes
//! back to its counter for anyone holding the key.
//!
//! This crate implements format version 1 of the
//! [dealcode spec](https://github.com/algorix-hq/dealcode/blob/main/SPEC.md) exactly; codes are
//! byte-for-byte interoperable with every other conforming implementation.
//!
//! # Quickstart
//!
//! ```
//! use dealcode::Dealcode;
//!
//! let codec = Dealcode::new("0a1b...64-hex-chars-from-your-secret-manager")?;
//!
//! let code = codec.encode(1)?;        // "421163" — 6 hex chars
//! assert_eq!(code.len(), 6);
//! assert_eq!(codec.decode(&code)?, 1); // never collides with any other counter
//! # Ok::<(), dealcode::Error>(())
//! ```
//!
//! Codes start at `min_length` characters and grow by one character only when
//! the current length is exhausted; the growth schedule, alphabet handling,
//! and tweak derivation are all fixed by the spec.
//!
//! # Configuration
//!
//! ```
//! use dealcode::Dealcode;
//!
//! let coupons = Dealcode::builder("your-secret-key")
//!     .alphabet("crockford")   // human-friendly codes, e.g. "7Q4WKZ"
//!     .min_length(6)
//!     .max_length(10)
//!     .domain("coupons")       // same key, unrelated codes per domain
//!     .build()?;
//!
//! let orders = Dealcode::builder("your-secret-key")
//!     .alphabet("dec")         // digits only
//!     .min_length(8)
//!     .domain("orders")
//!     .build()?;
//!
//! let n = coupons.decode(&coupons.encode(42)?)?;
//! assert_eq!(n, 42);
//! # Ok::<(), dealcode::Error>(())
//! ```
//!
//! **Immutability rule:** for a given code namespace, the entire
//! configuration — key, alphabet, `min_length`, `max_length`, `domain` — must
//! never change once codes have been issued.
//!
//! # Keys
//!
//! Any non-empty key material is accepted ([`Key`]): raw bytes of exactly
//! 16/24/32 bytes are used directly as the AES key; any other bytes, and
//! *all* strings (a hex-looking string is not auto-decoded), are expanded to
//! an AES-256 key via `SHA-256("dealcode/v1/kdf" ‖ material)`. Prefer at
//! least 128 bits of randomness, e.g. `openssl rand -hex 32`.

#![forbid(unsafe_code)]
#![warn(missing_docs)]

use std::fmt;

use sha2::{Digest, Sha256};

pub mod alphabets;
mod cycle;
mod ff1;

pub use cycle::{CyclingBuilder, CyclingDealcode};

use alphabets::ResolvedAlphabet;
use ff1::AesCipher;

/// Counters live in `[0, min(radix^max_length, 2^63))`.
const COUNTER_BOUND: u128 = 1 << 63;
/// The FF1 tweak is `"dealcode/v1/" + domain`.
const TWEAK_PREFIX: &str = "dealcode/v1/";
/// Key derivation prefix: `AES-256 key = SHA-256("dealcode/v1/kdf" ‖ material)`.
const KDF_PREFIX: &[u8] = b"dealcode/v1/kdf";

// ---------------------------------------------------------------------------
// Errors
// ---------------------------------------------------------------------------

/// All the ways dealcode can refuse to work, in three distinguishable kinds
/// (SPEC.md §8).
///
/// Invalid input is always rejected — never silently truncated, wrapped, or
/// "fixed".
#[derive(Debug, Clone, PartialEq, Eq)]
#[non_exhaustive]
pub enum Error {
    /// Invalid codec configuration: key, alphabet, lengths, or domain.
    /// Raised at construction time only.
    Config(String),
    /// [`Dealcode::encode`] was called with a counter outside
    /// `[0, capacity)` — or, in cycling mode, [`CyclingDealcode::encode`]
    /// with a counter at or beyond `2^63`, or
    /// [`CyclingDealcode::decode`] with a cycle beyond
    /// [`max_cycle`](CyclingDealcode::max_cycle).
    Range {
        /// The out-of-range counter (or cycle number).
        n: u64,
        /// The exclusive upper bound the value had to stay below.
        capacity: u64,
    },
    /// [`Dealcode::decode`] input failed length, charset, or stage-range
    /// validation — this codec never issued it.
    InvalidCode(String),
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Error::Config(msg) => write!(f, "invalid configuration: {msg}"),
            Error::Range { n, capacity } => {
                write!(f, "counter {n} out of range [0, {capacity})")
            }
            Error::InvalidCode(msg) => write!(f, "invalid code: {msg}"),
        }
    }
}

impl std::error::Error for Error {}

// ---------------------------------------------------------------------------
// Key material
// ---------------------------------------------------------------------------

/// Key material for a [`Dealcode`] codec (SPEC.md §2.1).
///
/// Built via `From`/`Into`, so anything key-shaped can be passed straight to
/// [`Dealcode::new`] or [`Dealcode::builder`]:
///
/// - **Bytes** (`[u8; N]`, `&[u8]`, `Vec<u8>`) of length exactly 16, 24, or
///   32 are used directly as the AES key; any other non-empty length is
///   expanded to an AES-256 key.
/// - **Strings** (`&str`, `String`) are *always* expanded from their UTF-8
///   bytes — a hex-looking string is not auto-decoded.
///
/// Expansion is `AES-256 key = SHA-256("dealcode/v1/kdf" ‖ material)`. Empty
/// keys are rejected at build time with [`Error::Config`].
///
/// ```
/// use dealcode::Dealcode;
///
/// let from_string = Dealcode::new("correct horse battery staple")?;
/// let from_bytes = Dealcode::new([0x2b_u8; 32])?; // raw AES-256 key
/// # let _ = (from_string, from_bytes);
/// # Ok::<(), dealcode::Error>(())
/// ```
#[derive(Clone)]
pub struct Key(KeyRepr);

#[derive(Clone)]
enum KeyRepr {
    Bytes(Vec<u8>),
    Text(String),
}

impl Key {
    /// Resolves the material to a 16-, 24-, or 32-byte AES key.
    fn resolve(&self) -> Result<Vec<u8>, Error> {
        let (material, is_bytes) = match &self.0 {
            KeyRepr::Bytes(b) => (b.as_slice(), true),
            KeyRepr::Text(s) => (s.as_bytes(), false),
        };
        if material.is_empty() {
            return Err(Error::Config("key must not be empty".to_owned()));
        }
        // SPEC §2.1: string key material must not contain U+0000 (byte
        // material is unrestricted). Rust strings are always valid UTF-8, so
        // unpaired surrogates cannot occur here.
        if !is_bytes && material.contains(&0) {
            return Err(Error::Config(
                "string key material must not contain U+0000".to_owned(),
            ));
        }
        // SPEC §2.1: a string key that ASCII-case-insensitively equals a
        // preset alphabet name is almost certainly a swapped argument; no
        // real key material collides with this tiny set. Bytes keys are
        // unaffected.
        if let KeyRepr::Text(s) = &self.0 {
            if alphabets::PRESET_NAMES.contains(&s.to_ascii_lowercase().as_str()) {
                return Err(Error::Config(format!(
                    "string key {s:?} is a preset alphabet name — \
                     did you swap the key and alphabet arguments?"
                )));
            }
        }
        if is_bytes && matches!(material.len(), 16 | 24 | 32) {
            return Ok(material.to_vec());
        }
        let mut hasher = Sha256::new();
        hasher.update(KDF_PREFIX);
        hasher.update(material);
        Ok(hasher.finalize().to_vec())
    }
}

// Key material must never leak through Debug output or logs.
impl fmt::Debug for Key {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str("Key(..)")
    }
}

impl From<&str> for Key {
    fn from(s: &str) -> Self {
        Key(KeyRepr::Text(s.to_owned()))
    }
}

impl From<String> for Key {
    fn from(s: String) -> Self {
        Key(KeyRepr::Text(s))
    }
}

impl From<&[u8]> for Key {
    fn from(b: &[u8]) -> Self {
        Key(KeyRepr::Bytes(b.to_vec()))
    }
}

impl From<Vec<u8>> for Key {
    fn from(b: Vec<u8>) -> Self {
        Key(KeyRepr::Bytes(b))
    }
}

impl<const N: usize> From<[u8; N]> for Key {
    fn from(b: [u8; N]) -> Self {
        Key(KeyRepr::Bytes(b.to_vec()))
    }
}

impl<const N: usize> From<&[u8; N]> for Key {
    fn from(b: &[u8; N]) -> Self {
        Key(KeyRepr::Bytes(b.to_vec()))
    }
}

// ---------------------------------------------------------------------------
// Builder
// ---------------------------------------------------------------------------

/// Configures and builds a [`Dealcode`] codec.
///
/// Created by [`Dealcode::builder`]. Every option has a spec-defined default;
/// [`Builder::build`] validates the whole configuration (SPEC.md §2) and
/// returns [`Error::Config`] with a helpful message on any violation.
///
/// ```
/// use dealcode::Dealcode;
///
/// let codec = Dealcode::builder("your-secret-key")
///     .alphabet("crockford")
///     .min_length(6)
///     .max_length(10)
///     .domain("orders")
///     .build()?;
/// assert_eq!(codec.radix(), 32);
/// # Ok::<(), dealcode::Error>(())
/// ```
#[derive(Clone, Debug)]
#[must_use = "call .build() to construct the codec"]
pub struct Builder {
    key: Key,
    alphabet: String,
    min_length: usize,
    max_length: Option<usize>,
    domain: String,
}

impl Builder {
    /// Sets the alphabet: a preset name (`"dec"`, `"hex"`, `"base32"`,
    /// `"crockford"`, `"base36"`, `"base58"`, `"base62"`, `"base64url"`) or a
    /// custom alphabet string of 2–94 distinct printable ASCII characters.
    /// Preset names win on conflict. Default: `"hex"`.
    pub fn alphabet(mut self, alphabet: impl Into<String>) -> Self {
        self.alphabet = alphabet.into();
        self
    }

    /// Sets the length of first-stage codes. Must satisfy `min_length >= 2`
    /// and `radix^min_length >= 100`. Default: `6`.
    pub fn min_length(mut self, min_length: usize) -> Self {
        self.min_length = min_length;
        self
    }

    /// Sets the maximum code length. Must satisfy
    /// `min_length <= max_length` and `radix^max_length <= 2^128`.
    /// Default: the largest `L` with `radix^L <= 2^63 - 1` (e.g. 15 for hex,
    /// 18 for dec, 12 for crockford).
    ///
    /// Set `max_length == min_length` for fixed-length codes.
    pub fn max_length(mut self, max_length: usize) -> Self {
        self.max_length = Some(max_length);
        self
    }

    /// Sets the domain, an application-chosen namespace label (e.g.
    /// `"orders"`). Two codecs with the same key but different domains
    /// produce unrelated permutations. At most 255 UTF-8 bytes.
    /// Default: `""`.
    pub fn domain(mut self, domain: impl Into<String>) -> Self {
        self.domain = domain.into();
        self
    }

    /// Validates the configuration and builds the codec.
    pub fn build(self) -> Result<Dealcode, Error> {
        Dealcode::from_builder(self)
    }
}

// ---------------------------------------------------------------------------
// Codec
// ---------------------------------------------------------------------------

/// A dealcode codec: a bijective `counter <-> code` mapping (SPEC.md).
///
/// Instances are immutable, `Send + Sync`, and cheap to keep around (AES
/// round keys and per-length FF1 parameters are precomputed once): create one
/// per code namespace at startup and share it, including across threads.
///
/// ```
/// use dealcode::Dealcode;
///
/// let codec = Dealcode::new("example-key")?;
/// assert_eq!(codec.decode(&codec.encode(42)?)?, 42);
/// # Ok::<(), dealcode::Error>(())
/// ```
#[derive(Clone)]
pub struct Dealcode {
    alphabet: ResolvedAlphabet,
    /// Maps an ASCII byte to its numeral value; `NO_NUMERAL` if absent.
    char_index: [u8; 128],
    radix: u32,
    min_length: usize,
    max_length: usize,
    domain: String,
    /// `min(radix^max_length, 2^63)` — the number of encodable counters.
    capacity: u64,
    cipher: AesCipher,
    /// `powers[d] = radix^d` for `d` in `[0, max_length - 1]`. `radix^max_length`
    /// is deliberately absent: it may equal `2^128`, which does not fit u128.
    powers: Vec<u128>,
    /// Precomputed FF1 parameters for each code length in
    /// `[min_length, max_length]`.
    params: Vec<ff1::Params>,
}

/// Sentinel in [`Dealcode::char_index`] for "not in the alphabet".
const NO_NUMERAL: u8 = 0xFF;

impl Dealcode {
    /// Builds a codec with all defaults: `hex` alphabet, `min_length` 6,
    /// spec-default `max_length` (15 for hex), empty domain.
    ///
    /// ```
    /// use dealcode::Dealcode;
    ///
    /// let codec = Dealcode::new("example-key")?;
    /// assert_eq!(codec.alphabet(), "0123456789abcdef");
    /// assert_eq!((codec.min_length(), codec.max_length()), (6, 15));
    /// # Ok::<(), dealcode::Error>(())
    /// ```
    pub fn new(key: impl Into<Key>) -> Result<Self, Error> {
        Self::builder(key).build()
    }

    /// Starts building a codec from key material. See [`Builder`].
    pub fn builder(key: impl Into<Key>) -> Builder {
        Builder {
            key: key.into(),
            alphabet: "hex".to_owned(),
            min_length: 6,
            max_length: None,
            domain: String::new(),
        }
    }

    fn from_builder(builder: Builder) -> Result<Self, Error> {
        let aes_key = builder.key.resolve()?;
        let alphabet = alphabets::resolve(&builder.alphabet).map_err(Error::Config)?;
        let radix = alphabet.chars.chars().count() as u32;
        let r = u128::from(radix);

        let min_length = builder.min_length;
        if !(2..=128).contains(&min_length) {
            return Err(Error::Config(
                "min_length must be in [2, 128]".to_owned(),
            ));
        }
        // radix^min_length >= 100; overflow (None) trivially satisfies it.
        let min_ok = u32::try_from(min_length)
            .ok()
            .map_or(true, |e| r.checked_pow(e).map_or(true, |p| p >= 100));
        if !min_ok {
            return Err(Error::Config(
                "radix^min_length must be at least 100 (FF1 minimum domain)".to_owned(),
            ));
        }

        let max_length = builder
            .max_length
            .unwrap_or_else(|| default_max_length(r, min_length));
        if max_length < min_length {
            return Err(Error::Config(
                "max_length must be at least min_length".to_owned(),
            ));
        }
        // radix^max_length <= 2^128, checked without ever materializing
        // 2^128: equivalent to radix^(max_length-1) <= floor(2^128 / radix).
        // Since radix >= 2, any max_length > 128 necessarily violates it.
        let floor_2_128_div_r = (u128::MAX - (r - 1)) / r + 1;
        let max_ok = max_length <= 128
            && r
                .checked_pow((max_length - 1) as u32)
                .map_or(false, |p| p <= floor_2_128_div_r);
        if !max_ok {
            return Err(Error::Config(
                "radix^max_length must not exceed 2^128".to_owned(),
            ));
        }

        // SPEC §2.1: domains must not contain U+0000 (Rust strings are
        // always valid UTF-8, so unpaired surrogates cannot occur).
        if builder.domain.as_bytes().contains(&0) {
            return Err(Error::Config(
                "domain must not contain U+0000".to_owned(),
            ));
        }
        if builder.domain.len() > 255 {
            return Err(Error::Config(
                "domain must be at most 255 UTF-8 bytes".to_owned(),
            ));
        }

        let mut char_index = [NO_NUMERAL; 128];
        for (i, c) in alphabet.chars.chars().enumerate() {
            char_index[c as usize] = i as u8; // ASCII by validation; i < 94
        }

        let tweak = [TWEAK_PREFIX.as_bytes(), builder.domain.as_bytes()].concat();

        // radix^d for d in [0, max_length - 1]; all fit in u128 because
        // radix^(max_length-1) <= 2^128 / radix.
        let powers: Vec<u128> = (0..max_length as u32).map(|d| r.pow(d)).collect();

        let capacity = match r.checked_pow(max_length as u32) {
            Some(p) if p < COUNTER_BOUND => p as u64,
            _ => COUNTER_BOUND as u64, // includes radix^max_length == 2^128
        };

        let cipher = AesCipher::new(&aes_key);
        let params = (min_length..=max_length)
            .map(|d| ff1::params(radix, &tweak, d))
            .collect();

        Ok(Dealcode {
            alphabet,
            char_index,
            radix,
            min_length,
            max_length,
            domain: builder.domain,
            capacity,
            cipher,
            powers,
            params,
        })
    }

    // -- introspection ------------------------------------------------------

    /// The canonical alphabet characters, in numeral order.
    pub fn alphabet(&self) -> &str {
        &self.alphabet.chars
    }

    /// The number of characters in the alphabet.
    pub fn radix(&self) -> u32 {
        self.radix
    }

    /// The length of first-stage codes.
    pub fn min_length(&self) -> usize {
        self.min_length
    }

    /// The maximum code length.
    pub fn max_length(&self) -> usize {
        self.max_length
    }

    /// The domain (namespace label) bound into the FF1 tweak.
    pub fn domain(&self) -> &str {
        &self.domain
    }

    /// The number of encodable counters: `min(radix^max_length, 2^63)`.
    ///
    /// [`Dealcode::encode`] accepts exactly `0..capacity`.
    ///
    /// ```
    /// use dealcode::Dealcode;
    ///
    /// let codec = Dealcode::new("example-key")?; // hex, max_length 15
    /// assert_eq!(codec.capacity(), 16u64.pow(15));
    /// # Ok::<(), dealcode::Error>(())
    /// ```
    pub fn capacity(&self) -> u64 {
        self.capacity
    }

    // -- public API ---------------------------------------------------------

    /// Maps counter `n` to its code. O(1) in `n`.
    ///
    /// Returns [`Error::Range`] if `n >= self.capacity()`.
    ///
    /// ```
    /// use dealcode::Dealcode;
    ///
    /// let codec = Dealcode::new("example-key")?;
    /// let code = codec.encode(7)?;
    /// assert_eq!(code.len(), codec.min_length());
    /// # Ok::<(), dealcode::Error>(())
    /// ```
    pub fn encode(&self, n: u64) -> Result<String, Error> {
        if n >= self.capacity {
            return Err(Error::Range { n, capacity: self.capacity });
        }
        let n = u128::from(n);

        // Stage: d = number of base-radix digits of n, but never < min_length
        // (SPEC.md §4). n < radix^max_length is guaranteed by the capacity
        // check, so the scan never needs powers[max_length].
        let mut d = self.min_length;
        while d < self.max_length && n >= self.powers[d] {
            d += 1;
        }
        let base = if d == self.min_length { 0 } else { self.powers[d - 1] };
        let mut v = n - base;

        // X = STR(v, radix, d), big-endian, zero-padded.
        let r = u128::from(self.radix);
        let mut numerals = vec![0u8; d];
        for slot in numerals.iter_mut().rev() {
            *slot = (v % r) as u8;
            v /= r;
        }

        let cipher_text = ff1::encrypt(
            &self.cipher,
            &self.params[d - self.min_length],
            self.radix,
            &numerals,
        );

        let alphabet = self.alphabet.chars.as_bytes();
        let bytes: Vec<u8> = cipher_text.iter().map(|&x| alphabet[x as usize]).collect();
        Ok(String::from_utf8(bytes).expect("alphabet is ASCII"))
    }

    /// Maps a code back to its counter.
    ///
    /// Returns [`Error::InvalidCode`] for anything this codec never issued:
    /// wrong length, characters outside the alphabet (after the alphabet's
    /// normalization, e.g. case-folding for `hex`), or a well-formed string
    /// whose decryption falls outside the counter space.
    ///
    /// Decode success only proves the code is *consistent* with the key; the
    /// application still decides whether counter `n` actually exists.
    ///
    /// ```
    /// use dealcode::{Dealcode, Error};
    ///
    /// let codec = Dealcode::new("example-key")?;
    /// let code = codec.encode(99)?;
    /// assert_eq!(codec.decode(&code)?, 99);
    /// assert_eq!(codec.decode(&code.to_uppercase())?, 99); // hex case-folds
    /// assert!(matches!(codec.decode("nope!"), Err(Error::InvalidCode(_))));
    /// # Ok::<(), dealcode::Error>(())
    /// ```
    pub fn decode(&self, code: &str) -> Result<u64, Error> {
        let d = code.chars().count();
        if d < self.min_length || d > self.max_length {
            return Err(Error::InvalidCode(format!(
                "code length {d} outside [{}, {}]",
                self.min_length, self.max_length
            )));
        }

        let mut numerals = Vec::with_capacity(d);
        for c in code.chars() {
            let c = self.alphabet.normalization.apply(c);
            let numeral = if c.is_ascii() { self.char_index[c as usize] } else { NO_NUMERAL };
            if numeral == NO_NUMERAL {
                return Err(Error::InvalidCode(format!("character {c:?} not in alphabet")));
            }
            numerals.push(numeral);
        }

        let plain = ff1::decrypt(
            &self.cipher,
            &self.params[d - self.min_length],
            self.radix,
            &numerals,
        );
        let v = ff1::num(u128::from(self.radix), &plain);

        // Stage-range check (SPEC.md §7): the code must decrypt into its
        // stage, and the counter must be inside [0, 2^63).
        let base = if d == self.min_length { 0 } else { self.powers[d - 1] };
        if d > self.min_length {
            // capacity(d) = radix^d - radix^(d-1) = base * (radix - 1);
            // computed this way it fits u128 even when radix^d = 2^128.
            let stage_capacity = base * (u128::from(self.radix) - 1);
            if v >= stage_capacity {
                return Err(Error::InvalidCode("code was not issued by this codec".to_owned()));
            }
        }
        let n = base + v;
        if n >= COUNTER_BOUND {
            return Err(Error::InvalidCode("code was not issued by this codec".to_owned()));
        }
        Ok(n as u64)
    }
}

// Keys never appear in Debug output.
impl fmt::Debug for Dealcode {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let name = match self.alphabet.name {
            Some(name) => name.to_owned(),
            None => format!("custom({})", self.radix),
        };
        f.debug_struct("Dealcode")
            .field("alphabet", &name)
            .field("min_length", &self.min_length)
            .field("max_length", &self.max_length)
            .field("domain", &self.domain)
            .finish_non_exhaustive()
    }
}

/// Default `max_length`: the largest `L >= min_length` such that
/// `radix^L <= 2^63 - 1`, or `min_length` itself if even that exceeds the
/// counter bound (SPEC.md §2).
fn default_max_length(radix: u128, min_length: usize) -> usize {
    let exp = match u32::try_from(min_length) {
        Ok(e) => e,
        Err(_) => return min_length, // astronomically large; rejected later
    };
    let mut length = min_length;
    let mut cap = match radix.checked_pow(exp) {
        Some(c) => c,
        None => return min_length, // radix^min_length > 2^128; rejected later
    };
    while cap.checked_mul(radix).map_or(false, |next| next < COUNTER_BOUND) {
        cap *= radix;
        length += 1;
    }
    length
}

// `Dealcode` must be shareable across threads (compile-time assertion).
const _: () = {
    const fn assert_send_sync<T: Send + Sync>() {}
    assert_send_sync::<Dealcode>();
    assert_send_sync::<CyclingDealcode>();
    assert_send_sync::<CyclingBuilder>();
    assert_send_sync::<Key>();
    assert_send_sync::<Builder>();
    assert_send_sync::<Error>();
};
