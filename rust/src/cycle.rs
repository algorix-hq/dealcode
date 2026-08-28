//! Fixed-length cycling mode (SPEC.md §11, tweak namespace `dealcode/v1c/`).

use std::fmt;

use crate::alphabets::{self, ResolvedAlphabet};
use crate::ff1::{self, AesCipher};
use crate::{Error, Key, COUNTER_BOUND, NO_NUMERAL};

/// Cycling-mode FF1 tweaks are
/// `"dealcode/v1c/" + decimal(cycle) + "/" + domain`. The byte at offset 11
/// (`c` vs `/`) keeps the namespace disjoint from plain-v1 tweaks for every
/// possible domain and cycle.
const CYCLE_TWEAK_PREFIX: &str = "dealcode/v1c/";

/// Configures and builds a [`CyclingDealcode`] codec.
///
/// Created by [`CyclingDealcode::builder`]. Every option has a spec-defined
/// default; [`CyclingBuilder::build`] validates the whole configuration
/// (SPEC.md §11.1) and returns [`Error::Config`] with a helpful message on
/// any violation.
///
/// ```
/// use dealcode::CyclingDealcode;
///
/// let codec = CyclingDealcode::builder("your-secret-key")
///     .alphabet("crockford")
///     .length(6)
///     .domain("bookings")
///     .build()?;
/// assert_eq!(codec.capacity(), 32u64.pow(6));
/// # Ok::<(), dealcode::Error>(())
/// ```
#[derive(Clone, Debug)]
#[must_use = "call .build() to construct the codec"]
pub struct CyclingBuilder {
    key: Key,
    alphabet: String,
    length: usize,
    domain: String,
}

impl CyclingBuilder {
    /// Sets the alphabet: a preset name or a custom alphabet string, with
    /// exactly the rules of [`crate::Builder::alphabet`]. Default: `"hex"`.
    pub fn alphabet(mut self, alphabet: impl Into<String>) -> Self {
        self.alphabet = alphabet.into();
        self
    }

    /// Sets the fixed code length `L`: every code is exactly `L` characters
    /// in every cycle. Must satisfy `2 <= L <= 128` and
    /// `100 <= radix^L <= 2^63` (exactly `2^63` is allowed); for larger
    /// fixed spaces use [`crate::Dealcode`] with `min_length == max_length`.
    /// Default: `6`.
    pub fn length(mut self, length: usize) -> Self {
        self.length = length;
        self
    }

    /// Sets the domain (namespace label), with exactly the rules of
    /// [`crate::Builder::domain`]. Default: `""`.
    pub fn domain(mut self, domain: impl Into<String>) -> Self {
        self.domain = domain.into();
        self
    }

    /// Validates the configuration and builds the codec.
    pub fn build(self) -> Result<CyclingDealcode, Error> {
        CyclingDealcode::from_builder(self)
    }
}

/// A fixed-length cycling codec (dealcode mode v1c, SPEC.md §11): codes are
/// always exactly [`length`](CyclingDealcode::length) characters, and the
/// counter space is spent in cycles of
/// [`capacity`](CyclingDealcode::capacity)` = radix^length` codes each.
///
/// Counter `n` belongs to cycle `n / capacity` with in-cycle value
/// `n % capacity`; every cycle is a different permutation of the same code
/// space (a different FF1 tweak), so when the space is exhausted it refills
/// in a new order instead of growing.
///
/// # Codes repeat across cycles
///
/// The same strings recur in every cycle by design (pigeonhole: the space is
/// being refilled). Keep at most one cycle's codes live per uniqueness
/// scope — a global `UNIQUE(code)` index spanning cycles WILL fire; scope it
/// as `UNIQUE(cycle, code)` — and persist which cycle each live code belongs
/// to (or the currently active cycle): [`decode`](CyclingDealcode::decode)
/// needs it, and the library cannot recover the cycle from the code string.
///
/// ```
/// use dealcode::CyclingDealcode;
///
/// let codec = CyclingDealcode::builder("example-key")
///     .alphabet("crockford")
///     .length(6)
///     .build()?;
/// let n = 3 * codec.capacity() + 7; // cycle 3, in-cycle value 7
/// let code = codec.encode(n)?;
/// assert_eq!(code.len(), 6);
/// assert_eq!(codec.cycle_of(n)?, 3);
/// assert_eq!(codec.decode(&code, 3)?, n); // the cycle is required
/// # Ok::<(), dealcode::Error>(())
/// ```
///
/// Instances are immutable, `Send + Sync`, and cheap to keep around: create
/// one per code namespace at startup and share it, including across threads.
#[derive(Clone)]
pub struct CyclingDealcode {
    alphabet: ResolvedAlphabet,
    /// Maps an ASCII byte to its numeral value; `NO_NUMERAL` if absent.
    char_index: [u8; 128],
    radix: u32,
    length: usize,
    domain: String,
    /// `radix^length`; may be exactly `2^63` (which still fits `u64`).
    capacity: u64,
    /// `(2^63 - 1) / capacity`.
    max_cycle: u64,
    cipher: AesCipher,
}

impl CyclingDealcode {
    /// Builds a cycling codec with all defaults: `hex` alphabet, length 6,
    /// empty domain.
    ///
    /// ```
    /// use dealcode::CyclingDealcode;
    ///
    /// let codec = CyclingDealcode::new("example-key")?;
    /// assert_eq!((codec.alphabet(), codec.length()), ("0123456789abcdef", 6));
    /// # Ok::<(), dealcode::Error>(())
    /// ```
    pub fn new(key: impl Into<Key>) -> Result<Self, Error> {
        Self::builder(key).build()
    }

    /// Starts building a cycling codec from key material (same rules as
    /// [`crate::Dealcode::builder`]). See [`CyclingBuilder`].
    pub fn builder(key: impl Into<Key>) -> CyclingBuilder {
        CyclingBuilder {
            key: key.into(),
            alphabet: "hex".to_owned(),
            length: 6,
            domain: String::new(),
        }
    }

    fn from_builder(builder: CyclingBuilder) -> Result<Self, Error> {
        let aes_key = builder.key.resolve()?;
        let alphabet = alphabets::resolve(&builder.alphabet).map_err(Error::Config)?;
        let radix = alphabet.chars.chars().count() as u32;
        let r = u128::from(radix);

        // Bound the length BEFORE computing any power (SPEC §11.1).
        let length = builder.length;
        if !(2..=128).contains(&length) {
            return Err(Error::Config("length must be in [2, 128]".to_owned()));
        }
        // The per-cycle capacity C = radix^length must satisfy
        // 100 <= C <= 2^63 (exactly 2^63 is allowed, e.g. radix 8 and
        // length 21). Computed in u128 because 2^63 overflows i64 and the
        // power itself can exceed u64; checked_pow overflow (None) certainly
        // exceeds 2^63. length <= 128 always fits u32.
        let capacity = match r.checked_pow(length as u32) {
            Some(c) if c < 100 => {
                return Err(Error::Config(
                    "radix^length must be at least 100 (FF1 minimum domain)".to_owned(),
                ))
            }
            Some(c) if c <= COUNTER_BOUND => c as u64,
            _ => {
                return Err(Error::Config(
                    "radix^length must not exceed 2^63 in cycling mode — a cycle must \
                     be completable; use Dealcode with min_length == max_length for \
                     larger fixed spaces"
                        .to_owned(),
                ))
            }
        };

        // SPEC §2.1 via §11.1: domains must not contain U+0000 (Rust strings
        // are always valid UTF-8, so unpaired surrogates cannot occur).
        if builder.domain.as_bytes().contains(&0) {
            return Err(Error::Config("domain must not contain U+0000".to_owned()));
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

        Ok(CyclingDealcode {
            alphabet,
            char_index,
            radix,
            length,
            domain: builder.domain,
            capacity,
            max_cycle: (COUNTER_BOUND - 1) as u64 / capacity,
            cipher: AesCipher::new(&aes_key),
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

    /// The fixed code length: every code is exactly this many characters,
    /// in every cycle.
    pub fn length(&self) -> usize {
        self.length
    }

    /// The domain (namespace label) bound into the FF1 tweak.
    pub fn domain(&self) -> &str {
        &self.domain
    }

    /// The number of codes per cycle: `radix^length`. May be exactly `2^63`
    /// (the largest configuration cycling mode allows).
    pub fn capacity(&self) -> u64 {
        self.capacity
    }

    /// The largest usable cycle number: `(2^63 - 1) / capacity`.
    ///
    /// [`decode`](CyclingDealcode::decode) accepts cycles in
    /// `0..=max_cycle`.
    pub fn max_cycle(&self) -> u64 {
        self.max_cycle
    }

    /// The cycle that counter `n` belongs to: `n / capacity`.
    ///
    /// Returns [`Error::Range`] for `n >= 2^63` (the counter space is
    /// `[0, 2^63)` even though the parameter type is `u64`).
    pub fn cycle_of(&self, n: u64) -> Result<u64, Error> {
        if n >= COUNTER_BOUND as u64 {
            return Err(Error::Range { n, capacity: COUNTER_BOUND as u64 });
        }
        Ok(n / self.capacity)
    }

    // -- public API ---------------------------------------------------------

    /// Maps counter `n` to its fixed-length code (SPEC.md §11.2). The code
    /// belongs to cycle `n / capacity` — record that cycle (or the currently
    /// active cycle) to decode later.
    ///
    /// Returns [`Error::Range`] for `n >= 2^63`: the counter space is
    /// `[0, 2^63)`, so the top bit of the `u64` is never used.
    ///
    /// Codes repeat across cycles: `encode(n)` and `encode(n + capacity)`
    /// can return the same string for two different counters. See
    /// [`CyclingDealcode`].
    pub fn encode(&self, n: u64) -> Result<String, Error> {
        if n >= COUNTER_BOUND as u64 {
            return Err(Error::Range { n, capacity: COUNTER_BOUND as u64 });
        }
        // u64 arithmetic covers the capacity == 2^63 boundary configuration:
        // there cycle is always 0 and v == n.
        let cycle = n / self.capacity;
        let mut v = n % self.capacity;

        // X = STR(v, radix, length), big-endian, zero-padded.
        let r = u64::from(self.radix);
        let mut numerals = vec![0u8; self.length];
        for slot in numerals.iter_mut().rev() {
            *slot = (v % r) as u8;
            v /= r;
        }

        let params = ff1::params(self.radix, &self.tweak_for(cycle), self.length);
        let cipher_text = ff1::encrypt(&self.cipher, &params, self.radix, &numerals);

        let alphabet = self.alphabet.chars.as_bytes();
        let bytes: Vec<u8> = cipher_text.iter().map(|&x| alphabet[x as usize]).collect();
        Ok(String::from_utf8(bytes).expect("alphabet is ASCII"))
    }

    /// Maps a code issued in the given cycle back to its counter (SPEC.md
    /// §11.2). The cycle is required: the same string recurs in every cycle,
    /// mapping to a different counter each time, so a code alone is
    /// ambiguous by design.
    ///
    /// Returns [`Error::Range`] for a cycle above
    /// [`max_cycle`](CyclingDealcode::max_cycle), and [`Error::InvalidCode`]
    /// for anything this codec never issued in that cycle: wrong length,
    /// characters outside the alphabet (after the alphabet's normalization),
    /// or a counter at or beyond `2^63` (possible only in the final partial
    /// cycle).
    ///
    /// Decode success only proves the code is consistent with the key and
    /// cycle; the application still decides whether counter `n` actually
    /// exists.
    pub fn decode(&self, code: &str, cycle: u64) -> Result<u64, Error> {
        if cycle > self.max_cycle {
            // Reported as Range with the cycle bound: cycles must be
            // < max_cycle + 1 (which never overflows: max_cycle < 2^63).
            return Err(Error::Range { n: cycle, capacity: self.max_cycle + 1 });
        }
        // Length gate before normalization (SPEC §7 via §11.2):
        // normalization is length-preserving, so this is behaviour-identical
        // and rejects oversized garbage first.
        let d = code.chars().count();
        if d != self.length {
            return Err(Error::InvalidCode(format!(
                "code length {d} != {} (fixed-length mode)",
                self.length
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

        let params = ff1::params(self.radix, &self.tweak_for(cycle), self.length);
        let plain = ff1::decrypt(&self.cipher, &params, self.radix, &numerals);
        // v < capacity <= 2^63: the stage range check of SPEC §7 reduces to
        // v < capacity, which always holds for a full-width numeral string.
        let v = ff1::num(u128::from(self.radix), &plain);

        let n = u128::from(cycle) * u128::from(self.capacity) + v;
        if n >= COUNTER_BOUND {
            // Only reachable in the final partial cycle.
            return Err(Error::InvalidCode("code was not issued in this cycle".to_owned()));
        }
        Ok(n as u64)
    }

    /// Renders the cycling-mode FF1 tweak for one cycle:
    /// `"dealcode/v1c/" + decimal(cycle) + "/" + domain` (SPEC.md §11.2),
    /// with the cycle in base 10 and no leading zeros (`"0"` for cycle
    /// zero). With `domain <= 255` bytes and `cycle <= 2^63 - 1` the tweak
    /// is at most 288 bytes.
    fn tweak_for(&self, cycle: u64) -> Vec<u8> {
        format!("{CYCLE_TWEAK_PREFIX}{cycle}/{}", self.domain).into_bytes()
    }
}

// Keys never appear in Debug output.
impl fmt::Debug for CyclingDealcode {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let name = match self.alphabet.name {
            Some(name) => name.to_owned(),
            None => format!("custom({})", self.radix),
        };
        f.debug_struct("CyclingDealcode")
            .field("alphabet", &name)
            .field("length", &self.length)
            .field("domain", &self.domain)
            .finish_non_exhaustive()
    }
}
