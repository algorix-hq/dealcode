//! Behavioural tests: round trips across stage boundaries, configuration
//! validation, key material handling, thread safety, capacity limits, and
//! hostile decode input.

use dealcode::{alphabets, Dealcode, Error};

fn assert_send_sync<T: Send + Sync>() {}

// ---------------------------------------------------------------------------
// Round trips
// ---------------------------------------------------------------------------

/// Thousands of counters, including every stage boundary, must round-trip
/// with the stage-determined code length.
#[test]
fn roundtrip_with_stage_boundaries() {
    let configs: &[(&str, usize, usize)] = &[
        ("hex", 6, 15),
        ("dec", 4, 9),
        ("crockford", 6, 12),
        ("base62", 4, 12),
        ("BCDFGHJKLMNPQRSTVWXZ", 6, 14), // custom, radix 20
    ];
    for &(alphabet, min_length, max_length) in configs {
        let codec = Dealcode::builder("roundtrip-test-key")
            .alphabet(alphabet)
            .min_length(min_length)
            .max_length(max_length)
            .domain("roundtrip")
            .build()
            .unwrap();
        let radix = u128::from(codec.radix());
        let capacity = u128::from(codec.capacity());

        let mut counters: Vec<u64> = (0..2000).collect();
        // Every stage boundary r^d, plus its neighbours.
        for d in 1..=max_length as u32 {
            let boundary = radix.pow(d);
            for candidate in boundary.saturating_sub(2)..=boundary.saturating_add(2) {
                if candidate < capacity {
                    counters.push(candidate as u64);
                }
            }
        }
        counters.push(codec.capacity() - 1);
        // A deterministic scatter across the full range.
        let step = (codec.capacity() / 997).max(1);
        counters.extend((0..997u64).map(|i| (i * step) % codec.capacity()));

        for &n in &counters {
            let code = codec.encode(n).unwrap();

            // The code length is the number of base-radix digits of n,
            // never less than min_length (SPEC.md §4).
            let mut expected_len = min_length;
            while expected_len < max_length && u128::from(n) >= radix.pow(expected_len as u32) {
                expected_len += 1;
            }
            assert_eq!(code.chars().count(), expected_len, "{alphabet}: encode({n})");

            assert_eq!(codec.decode(&code).unwrap(), n, "{alphabet}: roundtrip {n}");
        }
    }
}

/// Distinct counters produce distinct codes (spot check within one stage).
#[test]
fn codes_are_unique() {
    let codec = Dealcode::new("uniqueness-key").unwrap();
    let mut seen = std::collections::HashSet::new();
    for n in 0..5000u64 {
        assert!(seen.insert(codec.encode(n).unwrap()), "collision at {n}");
    }
}

/// Same key, different domains: unrelated permutations.
#[test]
fn domains_are_independent() {
    let a = Dealcode::builder("shared-key").domain("orders").build().unwrap();
    let b = Dealcode::builder("shared-key").domain("coupons").build().unwrap();
    let plain = Dealcode::builder("shared-key").build().unwrap();
    for n in 0..50 {
        let code = a.encode(n).unwrap();
        assert_ne!(code, b.encode(n).unwrap());
        assert_ne!(code, plain.encode(n).unwrap());
    }
}

// ---------------------------------------------------------------------------
// Capacity and range behaviour
// ---------------------------------------------------------------------------

/// When radix^max_length > 2^63 the counter space is capped at exactly 2^63.
#[test]
fn capacity_capped_at_2_63() {
    // 16^16 = 2^64 > 2^63: capacity is exactly 2^63.
    let codec = Dealcode::builder("cap-key")
        .min_length(16)
        .max_length(16)
        .build()
        .unwrap();
    assert_eq!(codec.capacity(), 1u64 << 63);

    let last = (1u64 << 63) - 1;
    let code = codec.encode(last).unwrap();
    assert_eq!(code.len(), 16);
    assert_eq!(codec.decode(&code).unwrap(), last);

    for n in [1u64 << 63, (1u64 << 63) + 1, u64::MAX] {
        assert!(
            matches!(codec.encode(n), Err(Error::Range { .. })),
            "encode({n}) must be out of range"
        );
    }

    // 16^32 = 2^128 exactly: the spec's outer limit for the code space.
    let wide = Dealcode::builder("cap-key")
        .min_length(32)
        .max_length(32)
        .build()
        .unwrap();
    assert_eq!(wide.capacity(), 1u64 << 63);
    let code = wide.encode(last).unwrap();
    assert_eq!(code.len(), 32);
    assert_eq!(wide.decode(&code).unwrap(), last);
}

/// When radix^max_length < 2^63 the capacity is radix^max_length and the
/// Range error reports it.
#[test]
fn capacity_is_code_space_when_smaller() {
    let codec = Dealcode::builder("cap-key")
        .alphabet("dec")
        .min_length(4)
        .max_length(6)
        .build()
        .unwrap();
    assert_eq!(codec.capacity(), 1_000_000);
    assert!(codec.encode(999_999).is_ok());
    match codec.encode(1_000_000) {
        Err(Error::Range { n, capacity }) => {
            assert_eq!((n, capacity), (1_000_000, 1_000_000));
        }
        other => panic!("expected Range error, got {other:?}"),
    }
}

/// Spec-default max_length per preset (largest L with radix^L <= 2^63 - 1).
#[test]
fn default_max_lengths() {
    for (alphabet, expected) in [
        ("hex", 15),
        ("dec", 18),
        ("base32", 12),
        ("crockford", 12),
        ("base36", 12),
        ("base58", 10),
        ("base62", 10),
        ("base64url", 10),
    ] {
        let codec = Dealcode::builder("default-key").alphabet(alphabet).build().unwrap();
        assert_eq!(codec.max_length(), expected, "{alphabet}");
    }
    // If min_length already exceeds the 2^63 bound, the default collapses to
    // min_length (mirrors the reference implementation).
    let codec = Dealcode::builder("default-key").min_length(20).build().unwrap();
    assert_eq!(codec.max_length(), 20);
}

// ---------------------------------------------------------------------------
// Configuration validation
// ---------------------------------------------------------------------------

fn config_err(result: Result<Dealcode, Error>) -> String {
    match result {
        Err(Error::Config(msg)) => msg,
        other => panic!("expected Config error, got {other:?}"),
    }
}

#[test]
fn config_errors() {
    // Empty key material, in both shapes.
    config_err(Dealcode::new(""));
    config_err(Dealcode::new(Vec::<u8>::new()));
    config_err(Dealcode::new(&b""[..]));

    // Alphabets.
    config_err(Dealcode::builder("k").alphabet("a").build()); // too short
    config_err(Dealcode::builder("k").alphabet("abca").build()); // duplicate
    config_err(Dealcode::builder("k").alphabet("ab cd").build()); // space
    config_err(Dealcode::builder("k").alphabet("ab\u{e9}").build()); // non-ASCII
    config_err(Dealcode::builder("k").alphabet("ab\x1fcd").build()); // control char
    let too_long: String = (0x21u8..=0x7e).map(char::from).chain(['\x21']).collect();
    assert_eq!(too_long.len(), 95);
    config_err(Dealcode::builder("k").alphabet(too_long).build());

    // Lengths.
    config_err(Dealcode::builder("k").min_length(0).build());
    config_err(Dealcode::builder("k").min_length(1).build());
    config_err(Dealcode::builder("k").alphabet("dec").min_length(6).max_length(5).build());
    // radix^min_length < 100: 2 chars, min_length 6 -> 64.
    config_err(Dealcode::builder("k").alphabet("01").min_length(6).max_length(10).build());
    // radix^max_length > 2^128: 16^33 > 2^128.
    config_err(Dealcode::builder("k").min_length(6).max_length(33).build());
    config_err(Dealcode::builder("k").min_length(6).max_length(1000).build());

    // Domain over 255 UTF-8 bytes ("é" is 2 bytes: 128 chars = 256 bytes).
    config_err(Dealcode::builder("k").domain("x".repeat(256)).build());
    config_err(Dealcode::builder("k").domain("\u{e9}".repeat(128)).build());

    // Boundary cases that must succeed.
    Dealcode::builder("k").alphabet("dec").min_length(2).build().unwrap(); // 10^2 = 100
    Dealcode::builder("k").min_length(6).max_length(32).build().unwrap(); // 16^32 = 2^128
    Dealcode::builder("k").domain("x".repeat(255)).build().unwrap();
    Dealcode::builder("k").alphabet("01").min_length(7).max_length(128).build().unwrap();
}

#[test]
fn config_error_messages_are_helpful() {
    let msg = config_err(Dealcode::builder("k").min_length(1).build());
    assert!(msg.contains("min_length"), "{msg}");
    let msg = config_err(Dealcode::builder("k").max_length(33).build());
    assert!(msg.contains("2^128"), "{msg}");
    let msg = config_err(Dealcode::new(""));
    assert!(msg.contains("key"), "{msg}");
    // Display carries the message.
    let display = Dealcode::new("").unwrap_err().to_string();
    assert!(display.contains("key must not be empty"), "{display}");
}

// ---------------------------------------------------------------------------
// Key material
// ---------------------------------------------------------------------------

#[test]
fn key_material_flexibility() {
    let raw32 = [0x5au8; 32];

    // 32 bytes in any container shape: identical (direct AES-256 key).
    let a = Dealcode::new(raw32).unwrap();
    // Deliberately exercises the `From<&[u8; N]>` impl.
    #[allow(clippy::needless_borrows_for_generic_args)]
    let b = Dealcode::new(&raw32).unwrap();
    let c = Dealcode::new(raw32.to_vec()).unwrap();
    let d = Dealcode::new(&raw32[..]).unwrap();
    for n in 0..20 {
        let code = a.encode(n).unwrap();
        assert_eq!(code, b.encode(n).unwrap());
        assert_eq!(code, c.encode(n).unwrap());
        assert_eq!(code, d.encode(n).unwrap());
    }

    // AES-128 and AES-192 sized raw keys work too.
    Dealcode::new([1u8; 16]).unwrap().encode(1).unwrap();
    Dealcode::new([1u8; 24]).unwrap().encode(1).unwrap();

    // Strings always derive: &str and String agree.
    let s1 = Dealcode::new("passphrase").unwrap();
    let s2 = Dealcode::new(String::from("passphrase")).unwrap();
    assert_eq!(s1.encode(7).unwrap(), s2.encode(7).unwrap());

    // Bytes of a non-AES length derive from the same material as the equal
    // string, so they agree (both feed the same UTF-8 bytes to the KDF).
    let from_bytes = Dealcode::new(&b"passphrase"[..]).unwrap();
    assert_eq!(s1.encode(7).unwrap(), from_bytes.encode(7).unwrap());

    // A hex string is NOT auto-decoded: it derives, unlike the raw bytes.
    let hex_of_raw16 = "11".repeat(16); // hex spelling of [0x11; 16]
    let via_string = Dealcode::new(hex_of_raw16.as_str()).unwrap();
    let via_bytes = Dealcode::new([0x11u8; 16]).unwrap();
    assert_ne!(via_string.encode(0).unwrap(), via_bytes.encode(0).unwrap());

    // 16/24/32-byte STRINGS still derive (the bytes rule is bytes-only).
    let s16 = "0123456789abcdef"; // 16 UTF-8 bytes
    let derived = Dealcode::new(s16).unwrap();
    let direct = Dealcode::new(s16.as_bytes()).unwrap();
    assert_ne!(derived.encode(0).unwrap(), direct.encode(0).unwrap());
}

// ---------------------------------------------------------------------------
// Thread safety
// ---------------------------------------------------------------------------

#[test]
fn dealcode_is_send_and_sync() {
    assert_send_sync::<Dealcode>();
    assert_send_sync::<dealcode::Key>();
    assert_send_sync::<dealcode::Builder>();
    assert_send_sync::<Error>();
}

/// One shared codec, many threads, no locks: results must match the
/// single-threaded ones.
#[test]
fn threaded_usage() {
    let codec = Dealcode::builder("threads").alphabet("crockford").build().unwrap();
    let expected: Vec<String> = (0..400).map(|n| codec.encode(n).unwrap()).collect();

    std::thread::scope(|scope| {
        for t in 0..4u64 {
            let codec = &codec;
            let expected = &expected;
            scope.spawn(move || {
                for n in 0..400u64 {
                    let code = codec.encode(n).unwrap();
                    assert_eq!(code, expected[n as usize], "thread {t}");
                    assert_eq!(codec.decode(&code).unwrap(), n);
                }
            });
        }
    });
}

// ---------------------------------------------------------------------------
// Hostile input: no panics, only errors
// ---------------------------------------------------------------------------

#[test]
fn decode_never_panics_on_weird_input() {
    let codecs = [
        Dealcode::new("weird-input").unwrap(),
        Dealcode::builder("weird-input").alphabet("crockford").build().unwrap(),
        Dealcode::builder("weird-input").alphabet("base58").build().unwrap(),
        Dealcode::builder("weird-input").alphabet("!\"#$%&'()*").min_length(2).max_length(4).build().unwrap(),
    ];
    let inputs: &[&str] = &[
        "",
        " ",
        "\0\0\0\0\0\0",
        "abc",
        "ABCDEF",
        "abcdef",
        "000000",
        "??????",
        "\u{fffd}\u{fffd}\u{fffd}\u{fffd}\u{fffd}\u{fffd}",
        "héllo!",
        "ｆｕｌｌｗｉｄｔｈ",
        "深深深深深深",
        "a\u{0300}bcde",
        "123456789012345678901234567890123456789012345678901234567890",
        "\t\n\r\t\n\r",
        "ﬀﬀﬀﬀﬀﬀ", // ligatures: non-ASCII case handling
        "𝟶𝟷𝟸𝟹𝟺𝟻",
    ];
    let long = "a".repeat(100_000);
    for codec in &codecs {
        for &input in inputs {
            // Must return, never panic; correctness of Ok values is covered
            // by the vector tests.
            let _ = codec.decode(input);
        }
        assert!(codec.decode(&long).is_err());
    }
}

#[test]
fn decode_rejections_are_invalid_code() {
    let codec = Dealcode::builder("rejects").alphabet("dec").min_length(4).max_length(6).build().unwrap();
    for input in ["123", "1234567", "12a4", "-1234", " 1234", "12345\u{30c9}"] {
        assert!(
            matches!(codec.decode(input), Err(Error::InvalidCode(_))),
            "decode({input:?})"
        );
    }
}

/// Every decodable code must round-trip back to the same string; codes that
/// decode successfully are exactly the issued ones.
#[test]
fn decode_accepts_only_issued_codes() {
    // dec, min 4, max 4: exactly 10^4 codes exist and all decode.
    let codec = Dealcode::builder("closure").alphabet("dec").min_length(4).max_length(4).build().unwrap();
    let mut ok = 0u32;
    for v in 0..10_000u32 {
        let s = format!("{v:04}");
        if let Ok(n) = codec.decode(&s) {
            assert_eq!(codec.encode(n).unwrap(), s);
            ok += 1;
        }
    }
    assert_eq!(ok, 10_000); // fixed-length full stage: every string is a code

    // dec, min 4, max 5: 5-digit strings decoding into the first stage's
    // range are rejected, so exactly 10^5 - 10^4 of them decode.
    let codec = Dealcode::builder("closure").alphabet("dec").min_length(4).max_length(5).build().unwrap();
    let mut ok = 0u32;
    for v in 0..100_000u32 {
        let s = format!("{v:05}");
        if let Ok(n) = codec.decode(&s) {
            assert_eq!(codec.encode(n).unwrap(), s);
            ok += 1;
        }
    }
    assert_eq!(ok, 90_000);
}

// ---------------------------------------------------------------------------
// Misc API behaviour
// ---------------------------------------------------------------------------

#[test]
fn accessors() {
    let codec = Dealcode::builder("accessors")
        .alphabet("crockford")
        .min_length(6)
        .max_length(10)
        .domain("orders")
        .build()
        .unwrap();
    assert_eq!(codec.alphabet(), alphabets::CROCKFORD);
    assert_eq!(codec.radix(), 32);
    assert_eq!(codec.min_length(), 6);
    assert_eq!(codec.max_length(), 10);
    assert_eq!(codec.domain(), "orders");
    assert_eq!(codec.capacity(), 32u64.pow(10));
}

#[test]
fn debug_output_never_contains_key_material() {
    let secret = "super-secret-key-material";
    let codec = Dealcode::builder(secret).domain("orders").build().unwrap();
    let debug = format!("{codec:?}");
    assert!(!debug.contains(secret), "{debug}");
    assert!(debug.contains("orders"), "{debug}");

    let builder = Dealcode::builder(secret);
    let debug = format!("{builder:?}");
    assert!(!debug.contains(secret), "{debug}");
}

#[test]
fn errors_are_std_errors() {
    fn boxed(e: Error) -> Box<dyn std::error::Error> {
        Box::new(e)
    }
    let range = Dealcode::builder("k").alphabet("dec").min_length(4).max_length(4).build().unwrap()
        .encode(10_000)
        .unwrap_err();
    assert_eq!(range.to_string(), "counter 10000 out of range [0, 10000)");
    let invalid = Dealcode::new("k").unwrap().decode("!!!!!!").unwrap_err();
    assert!(boxed(invalid).to_string().starts_with("invalid code:"));
}

#[test]
fn codec_is_cloneable() {
    let codec = Dealcode::new("clone-me").unwrap();
    let clone = codec.clone();
    for n in 0..50 {
        assert_eq!(codec.encode(n).unwrap(), clone.encode(n).unwrap());
    }
}
