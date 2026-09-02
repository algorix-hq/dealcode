//! Integer range mode (SPEC.md §12, tweak namespace `dealcode/v1r/`).

use std::fmt;

use crate::ff1::{self, AesCipher};
use crate::{Error, Key, COUNTER_BOUND};

/// Range-mode FF1 tweaks are
/// `"dealcode/v1r/" + decimal(low) + "/" + decimal(high) + "/" + domain`.
/// The byte at offset 11 (`r` vs `c` vs `/`) keeps the namespace disjoint
/// from plain-v1 and cycling-mode tweaks for every possible configuration.
const RANGE_TWEAK_PREFIX: &str = "dealcode/v1r/";

/// Radix cap (SPEC §12.2): numerals stay one byte, matching the numeral
/// representation the FF1 core was validated with.
const MAX_RADIX: u64 = 256;

/// The largest integer `r` with `r^m <= n`, by binary search with
/// overflow-checked powers — exact integer arithmetic only; SPEC §12.2
/// forbids floating-point roots.
fn iroot(n: u64, m: u32) -> u64 {
    if n == 0 {
        return 0;
    }
    let n = u128::from(n);
    let pow_le = |r: u128| r.checked_pow(m).is_some_and(|p| p <= n);
    // Invariant: pow_le(lo) holds (1^m = 1 <= n), pow_le(hi) does not.
    let mut lo = 1u128;
    let mut hi = 2u128;
    while pow_le(hi) {
        hi *= 2;
    }
    while hi - lo > 1 {
        let mid = (lo + hi) / 2;
        if pow_le(mid) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    lo as u64
}

/// SPEC §12.2: `(radix, m, capacity)` — the largest `radix^m <= n` with
/// `radix` in `[2, 256]` and `m` in `[2, 63]`; the smallest `m` wins on
/// ties. Always finds a candidate for `n >= 100` (`m = 2` gives
/// `radix >= 10`, so `capacity >= 100`).
fn select_domain(n: u64) -> (u32, usize, u64) {
    let mut best_capacity = 0u64;
    let mut best = (0u32, 0usize);
    for m in 2..=63u32 {
        let r = iroot(n, m).min(MAX_RADIX);
        if r < 2 {
            continue;
        }
        let capacity = r.pow(m); // r <= iroot(n, m), so r^m <= n fits u64
        if capacity > best_capacity {
            // Strict '>' keeps the smallest m on ties.
            best_capacity = capacity;
            best = (r as u32, m as usize);
        }
    }
    (best.0, best.1, best_capacity)
}

/// Configures and builds a [`RangeDealcode`] codec.
///
/// Created by [`RangeDealcode::builder`] with the required `key`, `low`,
/// and `high`; the domain defaults to `""`. [`RangeBuilder::build`]
/// validates the whole configuration (SPEC.md §12.1) and returns
/// [`Error::Config`] with a helpful message on any violation.
///
/// ```
/// use dealcode::RangeDealcode;
///
/// let codec = RangeDealcode::builder("your-secret-key", 100_000, 999_999)
///     .domain("bookings")
///     .build()?;
/// assert_eq!(codec.capacity(), 884_736);
/// # Ok::<(), dealcode::Error>(())
/// ```
#[derive(Clone, Debug)]
#[must_use = "call .build() to construct the codec"]
pub struct RangeBuilder {
    key: Key,
    low: u64,
    high: u64,
    domain: String,
}

impl RangeBuilder {
    /// Sets the domain (namespace label), with exactly the rules of
    /// [`crate::Builder::domain`]. Default: `""`.
    pub fn domain(mut self, domain: impl Into<String>) -> Self {
        self.domain = domain.into();
        self
    }

    /// Validates the configuration and builds the codec.
    pub fn build(self) -> Result<RangeDealcode, Error> {
        RangeDealcode::from_builder(self)
    }
}

/// An integer range codec (dealcode mode v1r, SPEC.md §12): counters
/// `0 <= n < capacity` map bijectively to *integer* codes in
/// `[low, low + capacity - 1]` through a single FF1 call — no loops, no
/// cycle-walking.
///
/// [`capacity`](RangeDealcode::capacity) is the largest FF1 domain
/// (`radix^m` with `radix <= 256`) that fits in the range, so it can be
/// slightly smaller than `high - low + 1` (> 96 % of it for spans of at
/// least 10^5, and exact whenever the span is an admissible power); the
/// uncovered top slice is never issued and is rejected by
/// [`decode`](RangeDealcode::decode). Applications needing the top of the
/// range exactly should widen `high` until the capacity covers it.
///
/// Built for ranges like 100000–999999: every code is a 6-digit integer
/// with no leading zero, safe to store in an integer column.
///
/// ```
/// use dealcode::RangeDealcode;
///
/// let codec = RangeDealcode::new("example-key", 100_000, 999_999)?;
/// assert_eq!(codec.capacity(), 884_736); // 96^3 — 98.3% of the range
/// let code = codec.encode(0)?;           // an integer in [100000, 984735]
/// assert!((100_000..=984_735).contains(&code));
/// assert_eq!(codec.decode(code)?, 0);
/// # Ok::<(), dealcode::Error>(())
/// ```
///
/// Instances are immutable, `Send + Sync`, and cheap to keep around (AES
/// round keys and the FF1 parameters are precomputed once): create one per
/// code namespace at startup and share it, including across threads.
#[derive(Clone)]
pub struct RangeDealcode {
    low: u64,
    high: u64,
    domain: String,
    /// Internal FF1 radix (SPEC §12.2), in `[2, 256]`.
    radix: u32,
    /// Numeral-string length: codes are `m` base-`radix` numerals.
    m: usize,
    /// `radix^m` — the largest admissible power `<= high - low + 1`. May be
    /// exactly `2^63` (which still fits `u64`).
    capacity: u64,
    cipher: AesCipher,
    params: ff1::Params,
}

impl RangeDealcode {
    /// Builds a range codec with the default empty domain.
    ///
    /// ```
    /// use dealcode::RangeDealcode;
    ///
    /// let codec = RangeDealcode::new("example-key", 100_000, 999_999)?;
    /// assert_eq!((codec.low(), codec.high()), (100_000, 999_999));
    /// # Ok::<(), dealcode::Error>(())
    /// ```
    pub fn new(key: impl Into<Key>, low: u64, high: u64) -> Result<Self, Error> {
        Self::builder(key, low, high).build()
    }

    /// Starts building a range codec from key material (same rules as
    /// [`crate::Dealcode::builder`]) and the inclusive code range
    /// `[low, high]`. See [`RangeBuilder`].
    pub fn builder(key: impl Into<Key>, low: u64, high: u64) -> RangeBuilder {
        RangeBuilder { key: key.into(), low, high, domain: String::new() }
    }

    fn from_builder(builder: RangeBuilder) -> Result<Self, Error> {
        let aes_key = builder.key.resolve()?;

        let (low, high) = (builder.low, builder.high);
        if low > high || u128::from(high) >= COUNTER_BOUND {
            return Err(Error::Config(
                "low/high must satisfy 0 <= low <= high <= 2^63 - 1".to_owned(),
            ));
        }
        // high < 2^63, so the span fits u64 (it may be exactly 2^63).
        let span = high - low + 1;
        if span < 100 {
            return Err(Error::Config(
                "range must span at least 100 values (FF1 minimum domain)".to_owned(),
            ));
        }

        // SPEC §2.1 via §12.1: domains must not contain U+0000 (Rust strings
        // are always valid UTF-8, so unpaired surrogates cannot occur).
        if builder.domain.as_bytes().contains(&0) {
            return Err(Error::Config("domain must not contain U+0000".to_owned()));
        }
        if builder.domain.len() > 255 {
            return Err(Error::Config(
                "domain must be at most 255 UTF-8 bytes".to_owned(),
            ));
        }

        let (radix, m, capacity) = select_domain(span);

        // T = "dealcode/v1r/" + decimal(low) + "/" + decimal(high) + "/" +
        // domain (SPEC §12.3), fixed for the life of the codec. Binding low
        // and high makes different ranges unrelated permutations, exactly as
        // the domain does. At most 310 bytes.
        let tweak =
            format!("{RANGE_TWEAK_PREFIX}{low}/{high}/{}", builder.domain).into_bytes();

        Ok(RangeDealcode {
            low,
            high,
            domain: builder.domain,
            radix,
            m,
            capacity,
            cipher: AesCipher::new(&aes_key),
            params: ff1::params(radix, &tweak, m),
        })
    }

    // -- introspection ------------------------------------------------------

    /// The inclusive lower bound of the code range.
    pub fn low(&self) -> u64 {
        self.low
    }

    /// The inclusive upper bound of the code range. Codes at or above
    /// `low + capacity` are never issued (the dead zone).
    pub fn high(&self) -> u64 {
        self.high
    }

    /// The domain (namespace label) bound into the FF1 tweak.
    pub fn domain(&self) -> &str {
        &self.domain
    }

    /// The internal FF1 radix (SPEC §12.2); informational.
    pub fn radix(&self) -> u32 {
        self.radix
    }

    /// The number of issuable codes: the largest admissible `radix^m` that
    /// does not exceed `high - low + 1`. May be exactly `2^63` (the full
    /// counter space).
    ///
    /// [`RangeDealcode::encode`] accepts exactly `0..capacity`, and issues
    /// codes in `[low, low + capacity - 1]`.
    pub fn capacity(&self) -> u64 {
        self.capacity
    }

    // -- public API ---------------------------------------------------------

    /// Maps counter `n` to its integer code in `[low, low + capacity - 1]`
    /// (SPEC.md §12.3). O(1) in `n` — always exactly one FF1 call.
    ///
    /// Returns [`Error::Range`] if `n >= self.capacity()`; range mode has no
    /// staging and no cycles — when the range is exhausted, it is exhausted.
    pub fn encode(&self, n: u64) -> Result<u64, Error> {
        if n >= self.capacity {
            return Err(Error::Range { n, capacity: self.capacity });
        }
        let cipher_text =
            ff1::encrypt(&self.cipher, &self.params, self.radix, &self.to_numerals(n));
        // v < capacity <= 2^63 (FF1 permutes [0, radix^m)), and
        // low + capacity - 1 <= high < 2^63, so the sum cannot overflow.
        let v = ff1::num(u128::from(self.radix), &cipher_text) as u64;
        Ok(self.low + v)
    }

    /// Maps an integer code back to its counter (SPEC.md §12.3).
    ///
    /// Returns [`Error::InvalidCode`] for anything this codec never issued:
    /// a code outside `[low, high]`, or one in the dead zone
    /// `[low + capacity, high]` (the top slice of the range no admissible
    /// FF1 domain covers).
    ///
    /// Decode success only proves the code is *consistent* with the key and
    /// range; the application still decides whether counter `n` actually
    /// exists.
    pub fn decode(&self, code: u64) -> Result<u64, Error> {
        if code < self.low || code > self.high {
            return Err(Error::InvalidCode(format!(
                "code {code} outside range [{}, {}]",
                self.low, self.high
            )));
        }
        let v = code - self.low;
        if v >= self.capacity {
            return Err(Error::InvalidCode(format!(
                "code {code} in the unissued top slice of the range (capacity {})",
                self.capacity
            )));
        }
        let plain =
            ff1::decrypt(&self.cipher, &self.params, self.radix, &self.to_numerals(v));
        // n < capacity always holds (FF1 permutes [0, radix^m)), so no
        // further range check is needed.
        Ok(ff1::num(u128::from(self.radix), &plain) as u64)
    }

    /// `STR(value, radix, m)`: `value` as `m` big-endian base-`radix`
    /// numerals, zero-padded.
    fn to_numerals(&self, mut value: u64) -> Vec<u8> {
        let r = u64::from(self.radix);
        let mut numerals = vec![0u8; self.m];
        for slot in numerals.iter_mut().rev() {
            *slot = (value % r) as u8; // < radix <= 256, so one byte
            value /= r;
        }
        numerals
    }
}

// Keys never appear in Debug output.
impl fmt::Debug for RangeDealcode {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("RangeDealcode")
            .field("low", &self.low)
            .field("high", &self.high)
            .field("domain", &self.domain)
            .finish_non_exhaustive()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// SPEC §12.2 worked examples, including exact powers and the
    /// smallest-m tie rule.
    #[test]
    fn domain_selection_known_values() {
        assert_eq!(select_domain(100), (10, 2, 100));
        assert_eq!(select_domain(900_000), (96, 3, 884_736));
        assert_eq!(select_domain(1_000_000), (100, 3, 1_000_000)); // exact power
        assert_eq!(select_domain(1 << 63), (128, 9, 1 << 63)); // exact at the bound
        assert_eq!(select_domain(65_536), (256, 2, 65_536)); // tie -> smallest m
    }

    /// The selected capacity never exceeds the span, is maximal (bumping the
    /// radix would overshoot, unless it is already at the 256 cap), and
    /// covers > 96 % of any span of at least 10^5 (SPEC §12.2).
    #[test]
    fn domain_selection_capacity_bounds() {
        for n in [
            100u64,
            101,
            999,
            10_000,
            99_999,
            100_000,
            900_000,
            10_000_003,
            1 << 32,
            (1 << 53) + 1,
            (1 << 63) - 1,
            1 << 63,
        ] {
            let (radix, m, capacity) = select_domain(n);
            assert!((2..=256).contains(&radix), "radix for {n}");
            assert!((2..=63).contains(&m), "m for {n}");
            assert_eq!(u128::from(radix).pow(m as u32), u128::from(capacity));
            assert!(capacity <= n, "capacity for {n}");
            assert!(
                u128::from(radix + 1).pow(m as u32) > u128::from(n) || radix == 256,
                "radix not maximal for {n}"
            );
            if n >= 100_000 {
                // capacity / n > 96% <=> 25 * capacity > 24 * n, in u128 to
                // survive the 2^63 cases.
                assert!(
                    u128::from(capacity) * 25 > u128::from(n) * 24,
                    "capacity below 96% for {n}"
                );
            }
        }
    }

    /// `iroot` is exact at perfect powers and their neighbours.
    #[test]
    fn iroot_is_exact() {
        assert_eq!(iroot(0, 2), 0);
        for (n, m, root) in [
            (1u64, 2u32, 1u64),
            (99, 2, 9),
            (100, 2, 10),
            (101, 2, 10),
            (884_735, 3, 95),
            (884_736, 3, 96),
            (u64::MAX, 2, (1 << 32) - 1),
            (u64::MAX, 63, 2),
            (1 << 63, 63, 2),
            ((1 << 63) - 1, 63, 1),
        ] {
            assert_eq!(iroot(n, m), root, "iroot({n}, {m})");
        }
    }
}
