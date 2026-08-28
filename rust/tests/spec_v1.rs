//! Conformance against `testvectors/v1.json` — the dealcode format-v1
//! vectors across alphabets, stage boundaries, domains, normalization cases
//! and invalid codes. Passing this file (plus the NIST FF1 samples) is the
//! definition of conformance (SPEC.md §9).

use dealcode::{Dealcode, Error};
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
    min_length: usize,
    max_length: usize,
    domain: String,
    vectors: Vec<Vector>,
    invalid_codes: Vec<String>,
    normalize: Vec<Normalize>,
    /// Decimal strings that `encode` must reject with `Error::Range`; values
    /// not representable as u64 are covered by the type system instead.
    #[serde(default)]
    range_counters: Vec<String>,
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
    #[serde(default)]
    min_length: Option<usize>,
    #[serde(default)]
    max_length: Option<usize>,
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
struct Normalize {
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
    let path = concat!(env!("CARGO_MANIFEST_DIR"), "/../testvectors/v1.json");
    serde_json::from_str(&std::fs::read_to_string(path).expect("read v1.json"))
        .expect("parse v1.json")
}

fn build(config: &Config) -> Dealcode {
    let alphabet = match (config.alphabet.as_str(), &config.custom_alphabet) {
        ("custom", Some(custom)) => custom.as_str(),
        (preset, _) => preset,
    };
    let builder = match (&config.key_hex, &config.key_string) {
        (Some(hex), None) => Dealcode::builder(unhex(hex)),
        (None, Some(s)) => Dealcode::builder(s.as_str()),
        _ => panic!("config {} must have exactly one of key_hex/key_string", config.name),
    };
    builder
        .alphabet(alphabet)
        .min_length(config.min_length)
        .max_length(config.max_length)
        .domain(config.domain.as_str())
        .build()
        .unwrap_or_else(|e| panic!("config {} failed to build: {e}", config.name))
}

/// Every config: every vector encodes and decodes exactly; every invalid
/// code is rejected with `Error::InvalidCode`; every normalize case decodes
/// to its counter.
#[test]
fn v1_vectors() {
    let file = load();
    assert_eq!(file.spec, "dealcode/v1");
    assert!(!file.configs.is_empty());

    for config in &file.configs {
        let codec = build(config);
        assert!(!config.vectors.is_empty(), "{}: no vectors", config.name);

        for vector in &config.vectors {
            let n: u64 = vector.n.parse().expect("counter fits u64 (n < 2^63)");
            let encoded = codec
                .encode(n)
                .unwrap_or_else(|e| panic!("{}: encode({n}) failed: {e}", config.name));
            assert_eq!(encoded, vector.code, "{}: encode({n})", config.name);

            let decoded = codec
                .decode(&vector.code)
                .unwrap_or_else(|e| panic!("{}: decode({:?}) failed: {e}", config.name, vector.code));
            assert_eq!(decoded, n, "{}: decode({:?})", config.name, vector.code);
        }

        for code in &config.invalid_codes {
            match codec.decode(code) {
                Err(Error::InvalidCode(_)) => {}
                other => panic!(
                    "{}: decode({code:?}) should be Err(InvalidCode), got {other:?}",
                    config.name
                ),
            }
        }

        for case in &config.normalize {
            let n: u64 = case.n.parse().unwrap();
            assert_eq!(
                codec.decode(&case.input).ok(),
                Some(n),
                "{}: normalize {:?}",
                config.name,
                case.input
            );
        }

        for counter in &config.range_counters {
            // "-1" and "18446744073709551616" (2^64) do not fit u64: the
            // counter type itself rules them out, so there is nothing to
            // test. "9223372036854775808" (2^63) does parse and must be
            // rejected.
            let Ok(n) = counter.parse::<u64>() else { continue };
            match codec.encode(n) {
                Err(Error::Range { n: got, capacity }) => {
                    assert_eq!(got, n, "{}: Range error echoes the counter", config.name);
                    assert_eq!(capacity, codec.capacity(), "{}: Range error capacity", config.name);
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
fn v1_invalid_configs() {
    let file = load();
    assert!(!file.invalid_configs.is_empty());

    for config in &file.invalid_configs {
        let mut builder = match (&config.key_hex, &config.key_string) {
            (Some(hex), None) => Dealcode::builder(unhex(hex)),
            (None, Some(s)) => Dealcode::builder(s.as_str()),
            _ => panic!("config {} must have exactly one of key_hex/key_string", config.name),
        };
        let alphabet = match (&config.custom_alphabet, &config.alphabet) {
            (Some(custom), _) => custom.as_str(),
            (None, Some(preset)) => preset.as_str(),
            (None, None) => panic!("config {} has no alphabet", config.name),
        };
        builder = builder.alphabet(alphabet);
        if let Some(min_length) = config.min_length {
            builder = builder.min_length(min_length);
        }
        if let Some(max_length) = config.max_length {
            builder = builder.max_length(max_length);
        }
        if let Some(domain) = &config.domain {
            builder = builder.domain(domain.as_str());
        }
        match builder.build() {
            Err(Error::Config(_)) => {}
            other => panic!(
                "{}: build should be Err(Config), got {other:?}",
                config.name
            ),
        }
    }
}

/// The vectors' counters must round-trip through their neighbours too: for
/// every vector counter, encode/decode n-1, n, n+1 (when in range).
#[test]
fn v1_vector_neighbourhoods_roundtrip() {
    let file = load();
    for config in &file.configs {
        let codec = build(config);
        for vector in &config.vectors {
            let n: u64 = vector.n.parse().unwrap();
            for candidate in n.saturating_sub(1)..=n.saturating_add(1) {
                if candidate >= codec.capacity() {
                    continue;
                }
                let code = codec.encode(candidate).unwrap();
                assert_eq!(codec.decode(&code).unwrap(), candidate, "{}", config.name);
            }
        }
    }
}
