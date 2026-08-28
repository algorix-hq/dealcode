//! Fixed-length cycling mode (SPEC.md §11): conformance against
//! `testvectors/v1c.json`, plus behaviour tests.

use dealcode::{CyclingDealcode, Error};
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
    alphabet: String,
    #[serde(default)]
    custom_alphabet: Option<String>,
    #[serde(default)]
    key_hex: Option<String>,
    #[serde(default)]
    key_string: Option<String>,
    length: usize,
    domain: String,
    /// Decimal string: the octal-21 config's capacity is exactly 2^63,
    /// which exceeds i64 (and JSON's exact-double range).
    capacity: String,
    max_cycle: String,
    vectors: Vec<Vector>,
    invalid_codes: Vec<InvalidCode>,
    normalize: Vec<Normalize>,
    /// Decimal strings that `encode` must reject with `Error::Range`; values
    /// not representable as u64 are covered by the type system instead.
    range_counters: Vec<String>,
    /// Decimal strings that `decode` must reject as cycles with
    /// `Error::Range`.
    invalid_cycles: Vec<String>,
}

/// A configuration that must be rejected at build time with `Error::Config`.
#[derive(Deserialize)]
struct InvalidConfig {
    name: String,
    #[serde(default)]
    alphabet: Option<String>,
    #[serde(default)]
    custom_alphabet: Option<String>,
    #[serde(default)]
    key_hex: Option<String>,
    #[serde(default)]
    key_string: Option<String>,
    length: usize,
    #[serde(default)]
    domain: Option<String>,
}

#[derive(Deserialize)]
struct Vector {
    /// Decimal string: counters exceed 2^53, so JSON numbers are avoided.
    n: String,
    code: String,
}

#[derive(Deserialize)]
struct InvalidCode {
    cycle: String,
    code: String,
}

#[derive(Deserialize)]
struct Normalize {
    cycle: String,
    input: String,
    n: String,
}

fn unhex(s: &str) -> Vec<u8> {
    assert!(s.len() % 2 == 0, "odd-length hex");
    (0..s.len())
        .step_by(2)
        .map(|i| u8::from_str_radix(&s[i..i + 2], 16).unwrap())
        .collect()
}

fn load() -> File {
    let path = concat!(env!("CARGO_MANIFEST_DIR"), "/../testvectors/v1c.json");
    serde_json::from_str(&std::fs::read_to_string(path).expect("read v1c.json"))
        .expect("parse v1c.json")
}

fn build(config: &Config) -> CyclingDealcode {
    let alphabet = match (config.alphabet.as_str(), &config.custom_alphabet) {
        ("custom", Some(custom)) => custom.as_str(),
        (preset, _) => preset,
    };
    let builder = match (&config.key_hex, &config.key_string) {
        (Some(hex), None) => CyclingDealcode::builder(unhex(hex)),
        (None, Some(s)) => CyclingDealcode::builder(s.as_str()),
        _ => panic!("config {} must have exactly one of key_hex/key_string", config.name),
    };
    builder
        .alphabet(alphabet)
        .length(config.length)
        .domain(config.domain.as_str())
        .build()
        .unwrap_or_else(|e| panic!("config {} failed to build: {e}", config.name))
}

/// Every config: capacity/max_cycle match; every vector encodes and decodes
/// exactly under its cycle; every invalid code, out-of-range counter, and
/// out-of-range cycle is rejected with the right error kind; every
/// normalize case decodes to its counter.
#[test]
fn v1c_vectors() {
    let file = load();
    assert_eq!(file.spec, "dealcode/v1c");
    assert!(!file.configs.is_empty());

    for config in &file.configs {
        let codec = build(config);
        assert!(!config.vectors.is_empty(), "{}: no vectors", config.name);

        // "9223372036854775808" (2^63, the octal-21 capacity) parses as u64.
        let capacity: u64 = config.capacity.parse().expect("capacity fits u64");
        assert_eq!(codec.capacity(), capacity, "{}: capacity", config.name);
        let max_cycle: u64 = config.max_cycle.parse().unwrap();
        assert_eq!(codec.max_cycle(), max_cycle, "{}: max_cycle", config.name);

        for vector in &config.vectors {
            let n: u64 = vector.n.parse().expect("counter fits u64 (n < 2^63)");
            let encoded = codec
                .encode(n)
                .unwrap_or_else(|e| panic!("{}: encode({n}) failed: {e}", config.name));
            assert_eq!(encoded, vector.code, "{}: encode({n})", config.name);

            let cycle = codec.cycle_of(n).unwrap();
            assert_eq!(cycle, n / capacity, "{}: cycle_of({n})", config.name);
            let decoded = codec.decode(&vector.code, cycle).unwrap_or_else(|e| {
                panic!("{}: decode({:?}, {cycle}) failed: {e}", config.name, vector.code)
            });
            assert_eq!(decoded, n, "{}: decode({:?}, {cycle})", config.name, vector.code);
        }

        for invalid in &config.invalid_codes {
            let cycle: u64 = invalid.cycle.parse().unwrap();
            match codec.decode(&invalid.code, cycle) {
                Err(Error::InvalidCode(_)) => {}
                other => panic!(
                    "{}: decode({:?}, {cycle}) should be Err(InvalidCode), got {other:?}",
                    config.name, invalid.code
                ),
            }
        }

        for case in &config.normalize {
            let cycle: u64 = case.cycle.parse().unwrap();
            let n: u64 = case.n.parse().unwrap();
            assert_eq!(
                codec.decode(&case.input, cycle).ok(),
                Some(n),
                "{}: normalize {:?}",
                config.name,
                case.input
            );
        }

        for counter in &config.range_counters {
            // "-1" and "18446744073709551616" (2^64) do not fit u64: the
            // counter type itself rules them out, so there is nothing to
            // test. "9223372036854775808" (2^63) DOES parse as u64 and must
            // be rejected — cycling counters stop at 2^63 even though the
            // parameter type is u64.
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
            assert!(
                matches!(codec.cycle_of(n), Err(Error::Range { .. })),
                "{}: cycle_of({n}) should be Err(Range)",
                config.name
            );
        }

        let probe = codec.encode(0).unwrap();
        for cycle in &config.invalid_cycles {
            // "-1" does not fit u64: the cycle type itself rules it out.
            // Every over-limit cycle (max_cycle + 1) parses and must be
            // rejected.
            let Ok(cycle) = cycle.parse::<u64>() else { continue };
            match codec.decode(&probe, cycle) {
                Err(Error::Range { .. }) => {}
                other => panic!(
                    "{}: decode({probe:?}, {cycle}) should be Err(Range), got {other:?}",
                    config.name
                ),
            }
        }
    }
}

/// Every `invalid_configs` entry must be rejected at build time with
/// `Error::Config`.
#[test]
fn v1c_invalid_configs() {
    let file = load();
    assert!(!file.invalid_configs.is_empty());

    for config in &file.invalid_configs {
        let mut builder = match (&config.key_hex, &config.key_string) {
            (Some(hex), None) => CyclingDealcode::builder(unhex(hex)),
            (None, Some(s)) => CyclingDealcode::builder(s.as_str()),
            _ => panic!("config {} must have exactly one of key_hex/key_string", config.name),
        };
        let alphabet = match (&config.custom_alphabet, &config.alphabet) {
            (Some(custom), _) => custom.as_str(),
            (None, Some(preset)) => preset.as_str(),
            (None, None) => panic!("config {} has no alphabet", config.name),
        };
        builder = builder.alphabet(alphabet).length(config.length);
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

/// A full cycle issues each of the `capacity` possible strings exactly once,
/// and consecutive cycles refill the same space in different orders.
#[test]
fn full_cycle_is_a_permutation_and_cycles_differ() {
    let codec = CyclingDealcode::builder("k")
        .alphabet("dec")
        .length(2) // capacity 100
        .build()
        .unwrap();
    assert_eq!(codec.capacity(), 100);

    let mut cycles: Vec<Vec<String>> = Vec::new();
    for e in 0..3u64 {
        let codes: Vec<String> =
            (0..100).map(|v| codec.encode(e * 100 + v).unwrap()).collect();
        let mut sorted = codes.clone();
        sorted.sort();
        sorted.dedup();
        assert_eq!(sorted.len(), 100, "cycle {e} is not a permutation");
        for (v, code) in codes.iter().enumerate() {
            assert_eq!(codec.decode(code, e).unwrap(), e * 100 + v as u64);
        }
        cycles.push(codes);
    }
    // Same space every cycle, refilled in a different order.
    assert_ne!(cycles[0], cycles[1]);
    assert_ne!(cycles[1], cycles[2]);
    assert_ne!(cycles[0], cycles[2]);
}

/// Decoding under the wrong cycle succeeds but yields a different counter —
/// the documented ambiguity: the cycle is context the application must keep.
#[test]
fn wrong_cycle_gives_a_different_counter() {
    let codec = CyclingDealcode::builder("k")
        .alphabet("crockford")
        .length(6)
        .build()
        .unwrap();
    let code = codec.encode(7).unwrap();
    assert_eq!(codec.decode(&code, 0).unwrap(), 7);
    assert_ne!(codec.decode(&code, 1).unwrap(), 7);
}

/// The counter space's top value 2^63 - 1 must round-trip in the final
/// partial cycle, and 2^63 (representable in u64, unlike in languages with
/// i64 counters) must be rejected with `Error::Range`.
#[test]
fn final_partial_cycle_boundary() {
    let codec = CyclingDealcode::builder("k").alphabet("dec").length(2).build().unwrap();
    let top = (1u64 << 63) - 1;
    let code = codec.encode(top).unwrap();
    let cycle = codec.cycle_of(top).unwrap();
    assert_eq!(cycle, codec.max_cycle());
    assert_eq!(codec.decode(&code, cycle).unwrap(), top);

    assert!(matches!(codec.encode(1u64 << 63), Err(Error::Range { .. })));
    assert!(matches!(codec.encode(u64::MAX), Err(Error::Range { .. })));
    assert!(matches!(codec.cycle_of(1u64 << 63), Err(Error::Range { .. })));

    // In the final partial cycle only counters below 2^63 exist: 2^63 - 1
    // ends on in-cycle value 7, so exactly 8 of the 100 strings decode and
    // 92 are rejected as never issued.
    let decoded = (0..100)
        .filter(|v| {
            let probe = format!("{v:02}");
            codec.decode(&probe, cycle).is_ok()
        })
        .count();
    assert_eq!(decoded as u64, top % codec.capacity() + 1);
}

/// The boundary configuration radix^length == 2^63 (octal, length 21) is
/// legal: capacity is exactly 2^63, max cycle is 0, and every counter below
/// 2^63 lives in cycle zero.
#[test]
fn capacity_exactly_2_pow_63() {
    let codec = CyclingDealcode::builder("k").alphabet("01234567").length(21).build().unwrap();
    assert_eq!(codec.capacity(), 1u64 << 63);
    assert_eq!(codec.max_cycle(), 0);

    for n in [0, 1, (1u64 << 63) - 1] {
        let code = codec.encode(n).unwrap();
        assert_eq!(code.len(), 21);
        assert_eq!(codec.cycle_of(n).unwrap(), 0);
        assert_eq!(codec.decode(&code, 0).unwrap(), n);
    }
    // One radix step further (hex^16) exceeds 2^63 and must not build.
    assert!(matches!(
        CyclingDealcode::builder("k").alphabet("hex").length(16).build(),
        Err(Error::Config(_))
    ));
    // Cycle 1 does not exist when max_cycle == 0.
    assert!(matches!(
        codec.decode(&"0".repeat(21), 1),
        Err(Error::Range { .. })
    ));
}

/// Cycling-mode construction applies the same guards as the plain codec.
#[test]
fn config_guards() {
    // Preset name as string key: swapped-arguments guard.
    assert!(matches!(
        CyclingDealcode::builder("crockford").alphabet("hex").length(6).build(),
        Err(Error::Config(_))
    ));
    // Custom alphabet that case-insensitively matches a preset name.
    assert!(matches!(
        CyclingDealcode::builder("k").alphabet("HEX").length(6).build(),
        Err(Error::Config(_))
    ));
    // Empty key.
    assert!(matches!(CyclingDealcode::new(""), Err(Error::Config(_))));
    // Length bounds, checked before any power is computed.
    for length in [0, 1, 129, usize::MAX] {
        assert!(matches!(
            CyclingDealcode::builder("k").alphabet("hex").length(length).build(),
            Err(Error::Config(_))
        ));
    }
    // radix^length below the FF1 structural minimum of 100.
    assert!(matches!(
        CyclingDealcode::builder("k").alphabet("abcdefghi").length(2).build(),
        Err(Error::Config(_))
    ));
    // Oversized domain.
    assert!(matches!(
        CyclingDealcode::builder("k").domain("x".repeat(256)).build(),
        Err(Error::Config(_))
    ));
    // Defaults mirror the plain codec: hex, length 6.
    let codec = CyclingDealcode::new("example-key").unwrap();
    assert_eq!((codec.alphabet(), codec.length()), ("0123456789abcdef", 6));
    assert_eq!(codec.capacity(), 16u64.pow(6));
}

/// The fixed-length gate runs before normalization, and normalization
/// applies exactly as in plain v1.
#[test]
fn length_gate_and_normalization() {
    let codec = CyclingDealcode::builder("k").alphabet("crockford").length(6).build().unwrap();
    let code = codec.encode(7).unwrap();
    // Case-folding and O->0 / I,L->1 folding still apply per cycle.
    assert_eq!(codec.decode(&code.to_lowercase(), 0).unwrap(), 7);
    for bad in ["", "SHORT", "TOOLONG", "1234567890123456789012345678901234567890"] {
        assert!(
            matches!(codec.decode(bad, 0), Err(Error::InvalidCode(_))),
            "decode({bad:?}) should be InvalidCode"
        );
    }
    // Debug output shows the public configuration only (never the key).
    let debug = format!("{codec:?}");
    assert!(debug.contains("CyclingDealcode") && debug.contains("crockford"));
}
