//! Integer range mode (SPEC.md §12): conformance against
//! `testvectors/v1r.json`, plus behaviour tests.

use dealcode::{Error, RangeDealcode};
use serde::Deserialize;

#[derive(Deserialize)]
struct File {
    spec: String,
    configs: Vec<Config>,
    invalid_configs: Vec<InvalidConfig>,
}

#[derive(Deserialize)]
struct Config {
    name: String,
    /// Decimal strings: bounds reach 2^63 - 1, beyond JSON's exact-double
    /// range.
    low: String,
    high: String,
    domain: String,
    radix: u32,
    m: u32,
    /// Decimal string: the full-counter-space capacity is exactly 2^63,
    /// which exceeds i64.
    capacity: String,
    #[serde(default)]
    key_hex: Option<String>,
    #[serde(default)]
    key_string: Option<String>,
    vectors: Vec<Vector>,
    /// Decimal strings that `decode` must reject with `Error::InvalidCode`;
    /// values not representable as u64 are covered by the type system
    /// instead.
    invalid_codes: Vec<String>,
    /// Decimal strings that `encode` must reject with `Error::Range`; same
    /// u64 caveat.
    range_counters: Vec<String>,
}

/// A configuration that must be rejected at build time with `Error::Config`.
#[derive(Deserialize)]
struct InvalidConfig {
    name: String,
    #[serde(default)]
    key_hex: Option<String>,
    #[serde(default)]
    key_string: Option<String>,
    low: String,
    high: String,
    #[serde(default)]
    domain: Option<String>,
}

#[derive(Deserialize)]
struct Vector {
    /// Decimal strings: counters and codes exceed 2^53, so JSON numbers are
    /// avoided.
    n: String,
    code: String,
}

fn unhex(s: &str) -> Vec<u8> {
    assert!(s.len() % 2 == 0, "odd-length hex");
    (0..s.len())
        .step_by(2)
        .map(|i| u8::from_str_radix(&s[i..i + 2], 16).unwrap())
        .collect()
}

fn load() -> File {
    let path = concat!(env!("CARGO_MANIFEST_DIR"), "/../testvectors/v1r.json");
    serde_json::from_str(&std::fs::read_to_string(path).expect("read v1r.json"))
        .expect("parse v1r.json")
}

fn build(config: &Config) -> RangeDealcode {
    let low: u64 = config.low.parse().expect("low fits u64");
    let high: u64 = config.high.parse().expect("high fits u64");
    let builder = match (&config.key_hex, &config.key_string) {
        (Some(hex), None) => RangeDealcode::builder(unhex(hex), low, high),
        (None, Some(s)) => RangeDealcode::builder(s.as_str(), low, high),
        _ => panic!("config {} must have exactly one of key_hex/key_string", config.name),
    };
    builder
        .domain(config.domain.as_str())
        .build()
        .unwrap_or_else(|e| panic!("config {} failed to build: {e}", config.name))
}

/// Every config: the derived radix and capacity match; every vector encodes
/// and decodes exactly; every invalid code and out-of-range counter is
/// rejected with the right error kind.
#[test]
fn v1r_vectors() {
    let file = load();
    assert_eq!(file.spec, "dealcode/v1r");
    assert!(!file.configs.is_empty());

    for config in &file.configs {
        let codec = build(config);
        assert!(!config.vectors.is_empty(), "{}: no vectors", config.name);

        // "9223372036854775808" (2^63, the full-counter-space capacity)
        // parses as u64.
        let capacity: u64 = config.capacity.parse().expect("capacity fits u64");
        assert_eq!(codec.radix(), config.radix, "{}: radix", config.name);
        assert_eq!(codec.capacity(), capacity, "{}: capacity", config.name);
        // The recorded m is consistent: capacity = radix^m.
        assert_eq!(
            u128::from(config.radix).pow(config.m),
            u128::from(capacity),
            "{}: m",
            config.name
        );

        for vector in &config.vectors {
            let n: u64 = vector.n.parse().expect("counter fits u64 (n < 2^63)");
            let code: u64 = vector.code.parse().expect("code fits u64 (code < 2^63)");
            let encoded = codec
                .encode(n)
                .unwrap_or_else(|e| panic!("{}: encode({n}) failed: {e}", config.name));
            assert_eq!(encoded, code, "{}: encode({n})", config.name);
            let decoded = codec
                .decode(code)
                .unwrap_or_else(|e| panic!("{}: decode({code}) failed: {e}", config.name));
            assert_eq!(decoded, n, "{}: decode({code})", config.name);
        }

        for invalid in &config.invalid_codes {
            // "18446744073709551616" (2^64) does not fit u64: the code type
            // itself rules it out, so there is nothing to test.
            // "9223372036854775808" (2^63) DOES parse as u64 and must be
            // rejected — codes stop at high <= 2^63 - 1.
            let Ok(code) = invalid.parse::<u64>() else { continue };
            match codec.decode(code) {
                Err(Error::InvalidCode(_)) => {}
                other => panic!(
                    "{}: decode({code}) should be Err(InvalidCode), got {other:?}",
                    config.name
                ),
            }
        }

        for counter in &config.range_counters {
            // "-1" and "18446744073709551616" (2^64) do not fit u64: the
            // counter type itself rules them out. "9223372036854775808"
            // (2^63) parses and must be rejected — counters stop at
            // capacity <= 2^63 even though the parameter type is u64.
            let Ok(n) = counter.parse::<u64>() else { continue };
            match codec.encode(n) {
                Err(Error::Range { n: got, .. }) => {
                    assert_eq!(got, n, "{}: Range error echoes the counter", config.name);
                }
                other => panic!(
                    "{}: encode({n}) should be Err(Range), got {other:?}",
                    config.name
                ),
            }
        }
    }
}

/// Every `invalid_configs` entry must be rejected at build time with
/// `Error::Config`.
#[test]
fn v1r_invalid_configs() {
    let file = load();
    assert!(!file.invalid_configs.is_empty());

    for config in &file.invalid_configs {
        // "-1" (the negative-low entry) does not fit u64: the bound type
        // itself rules it out. "9223372036854775808" (2^63, the
        // high-at-2pow63 entry) parses and must be rejected.
        let (Ok(low), Ok(high)) = (config.low.parse::<u64>(), config.high.parse::<u64>())
        else {
            continue;
        };
        let mut builder = match (&config.key_hex, &config.key_string) {
            (Some(hex), None) => RangeDealcode::builder(unhex(hex), low, high),
            (None, Some(s)) => RangeDealcode::builder(s.as_str(), low, high),
            _ => panic!("config {} must have exactly one of key_hex/key_string", config.name),
        };
        if let Some(domain) = &config.domain {
            builder = builder.domain(domain.as_str());
        }
        match builder.build() {
            Err(Error::Config(_)) => {}
            other => panic!("{}: build should be Err(Config), got {other:?}", config.name),
        }
    }
}

// ---------------------------------------------------------------------------
// Behaviour
// ---------------------------------------------------------------------------

/// A span that is exactly an admissible power has no dead zone: the counters
/// map onto the full range, each code issued exactly once.
#[test]
fn small_range_is_a_full_bijection() {
    let codec = RangeDealcode::new("k", 1_000, 1_120).unwrap(); // span 121 = 11^2
    assert_eq!((codec.radix(), codec.capacity()), (11, 121));

    let mut codes: Vec<u64> = (0..121).map(|n| codec.encode(n).unwrap()).collect();
    for (n, &code) in codes.iter().enumerate() {
        assert_eq!(codec.decode(code).unwrap(), n as u64);
    }
    codes.sort_unstable();
    assert_eq!(codes, (1_000..=1_120).collect::<Vec<u64>>());
}

/// The dead zone `[low + capacity, high]` is rejected by decode, while the
/// last issued code and the range bounds behave exactly per SPEC §12.3.
#[test]
fn dead_zone_rejected_but_issued_top_accepted() {
    let codec = RangeDealcode::new("k", 100_000, 999_999).unwrap();
    assert_eq!(codec.capacity(), 884_736); // 96^3
    let top_issued = codec.low() + codec.capacity() - 1; // 984735

    let top_code = codec.encode(codec.capacity() - 1).unwrap();
    assert!((codec.low()..=top_issued).contains(&top_code));
    assert_eq!(codec.decode(top_code).unwrap(), codec.capacity() - 1);

    for dead in [top_issued + 1, 999_999] {
        assert!(
            matches!(codec.decode(dead), Err(Error::InvalidCode(_))),
            "decode({dead}) should be InvalidCode (dead zone)"
        );
    }
    for outside in [0, 99_999, 1_000_000, u64::MAX] {
        assert!(
            matches!(codec.decode(outside), Err(Error::InvalidCode(_))),
            "decode({outside}) should be InvalidCode (outside range)"
        );
    }
    assert!(matches!(
        codec.encode(codec.capacity()),
        Err(Error::Range { n: 884_736, capacity: 884_736 })
    ));
}

/// `low`, `high`, and `domain` are all bound into the tweak: changing any
/// one of them yields an unrelated permutation.
#[test]
fn low_high_and_domain_bind_the_permutation() {
    let a = RangeDealcode::new("k", 100_000, 999_999).unwrap();
    let b = RangeDealcode::new("k", 100_000, 999_998).unwrap();
    let c = RangeDealcode::builder("k", 100_000, 999_999).domain("x").build().unwrap();
    let d = RangeDealcode::new("k", 99_999, 999_999).unwrap();

    // b and d select the same 96^3 domain as a; only the tweak differs.
    let outs: Vec<Vec<u64>> = [&a, &b, &c, &d]
        .iter()
        .map(|codec| (0..8).map(|n| codec.encode(n).unwrap()).collect())
        .collect();
    for (i, x) in outs.iter().enumerate() {
        for y in &outs[i + 1..] {
            assert_ne!(x, y);
        }
    }
}

/// Range-mode construction applies the same guards as the plain codec.
#[test]
fn config_guards() {
    // Preset name as string key: swapped-arguments guard.
    assert!(matches!(
        RangeDealcode::new("crockford", 100_000, 999_999),
        Err(Error::Config(_))
    ));
    // Empty key.
    assert!(matches!(RangeDealcode::new("", 100_000, 999_999), Err(Error::Config(_))));
    // low above high.
    assert!(matches!(RangeDealcode::new("k", 10, 9), Err(Error::Config(_))));
    // Span below the FF1 structural minimum of 100.
    assert!(matches!(RangeDealcode::new("k", 0, 98), Err(Error::Config(_))));
    // high at or beyond 2^63 (representable in u64, unlike in languages
    // with i64 bounds).
    assert!(matches!(RangeDealcode::new("k", 0, 1 << 63), Err(Error::Config(_))));
    assert!(matches!(RangeDealcode::new("k", 0, u64::MAX), Err(Error::Config(_))));
    // Oversized domain, and U+0000 in the domain.
    assert!(matches!(
        RangeDealcode::builder("k", 100_000, 999_999).domain("x".repeat(256)).build(),
        Err(Error::Config(_))
    ));
    assert!(matches!(
        RangeDealcode::builder("k", 100_000, 999_999).domain("a\0b").build(),
        Err(Error::Config(_))
    ));
    // The exact bound 2^63 - 1 is legal, and its span 2^63 selects 128^9.
    let codec = RangeDealcode::new("k", 0, (1 << 63) - 1).unwrap();
    assert_eq!((codec.radix(), codec.capacity()), (128, 1 << 63));
}

/// Debug output shows the public configuration only (never the key).
#[test]
fn debug_hides_key() {
    let codec =
        RangeDealcode::builder("super-secret", 100_000, 999_999).domain("d").build().unwrap();
    let debug = format!("{codec:?}");
    assert!(debug.contains("RangeDealcode") && debug.contains("100000"));
    assert!(!debug.contains("secret"));
}
