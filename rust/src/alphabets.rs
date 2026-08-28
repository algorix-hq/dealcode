//! Alphabet presets and normalization rules (SPEC.md §3).
//!
//! Each constant holds the canonical characters of a preset, in order: the
//! character at index `i` represents numeral value `i`. Pass a preset *name*
//! (`"hex"`, `"crockford"`, …) or a custom alphabet string to
//! [`Builder::alphabet`](crate::Builder::alphabet); these constants are
//! exported so applications can inspect or reuse the exact character sets.
//!
//! ```
//! let codec = dealcode::Dealcode::builder("example-key")
//!     .alphabet("crockford")
//!     .build()?;
//! assert_eq!(codec.alphabet(), dealcode::alphabets::CROCKFORD);
//! # Ok::<(), dealcode::Error>(())
//! ```

/// Decimal digits, radix 10. No decode normalization.
pub const DEC: &str = "0123456789";

/// Lowercase hexadecimal, radix 16. Decode ASCII-lowercases its input.
pub const HEX: &str = "0123456789abcdef";

/// RFC 4648 Base32, radix 32. Decode ASCII-uppercases its input.
pub const BASE32: &str = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

/// Crockford Base32, radix 32. Decode ASCII-uppercases its input, then maps
/// `O→0`, `I→1`, `L→1`.
pub const CROCKFORD: &str = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

/// Lowercase base 36. Decode ASCII-lowercases its input.
pub const BASE36: &str = "0123456789abcdefghijklmnopqrstuvwxyz";

/// Bitcoin Base58 (no `0`, `O`, `I`, `l`), radix 58. No decode normalization.
pub const BASE58: &str = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

/// Base 62: digits, uppercase, lowercase. No decode normalization.
pub const BASE62: &str = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

/// RFC 4648 §5 URL-safe Base64 alphabet, radix 64. No decode normalization.
pub const BASE64URL: &str =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

/// The decode-input normalization applied by an alphabet (SPEC.md §3.1).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum Normalization {
    /// Input must match the canonical characters exactly.
    None,
    /// Map only `A–Z` to `a–z`; everything else untouched.
    AsciiLower,
    /// Map only `a–z` to `A–Z`; everything else untouched.
    AsciiUpper,
    /// ASCII-uppercase, then `O→0`, `I→1`, `L→1`.
    Crockford,
}

impl Normalization {
    /// Normalizes a single decode-input character.
    pub(crate) fn apply(self, c: char) -> char {
        match self {
            Normalization::None => c,
            Normalization::AsciiLower => c.to_ascii_lowercase(),
            Normalization::AsciiUpper => c.to_ascii_uppercase(),
            Normalization::Crockford => match c.to_ascii_uppercase() {
                'O' => '0',
                'I' | 'L' => '1',
                other => other,
            },
        }
    }
}

/// The eight preset alphabet names (SPEC.md §3.1), for the misconfiguration
/// guards: a custom alphabet or a string key that ASCII-case-insensitively
/// equals one of these is rejected at build time.
pub(crate) const PRESET_NAMES: [&str; 8] = [
    "dec", "hex", "base32", "crockford", "base36", "base58", "base62", "base64url",
];

/// A resolved alphabet: canonical characters plus decode normalization.
#[derive(Clone, Debug)]
pub(crate) struct ResolvedAlphabet {
    /// Preset name, or `None` for a custom alphabet.
    pub(crate) name: Option<&'static str>,
    pub(crate) chars: String,
    pub(crate) normalization: Normalization,
}

/// Resolves a preset name or a custom alphabet string (SPEC.md §3.2).
/// Preset names win on conflict.
pub(crate) fn resolve(alphabet: &str) -> Result<ResolvedAlphabet, String> {
    let preset = |name, chars: &str, normalization| ResolvedAlphabet {
        name: Some(name),
        chars: chars.to_owned(),
        normalization,
    };
    match alphabet {
        "dec" => return Ok(preset("dec", DEC, Normalization::None)),
        "hex" => return Ok(preset("hex", HEX, Normalization::AsciiLower)),
        "base32" => return Ok(preset("base32", BASE32, Normalization::AsciiUpper)),
        "crockford" => return Ok(preset("crockford", CROCKFORD, Normalization::Crockford)),
        "base36" => return Ok(preset("base36", BASE36, Normalization::AsciiLower)),
        "base58" => return Ok(preset("base58", BASE58, Normalization::None)),
        "base62" => return Ok(preset("base62", BASE62, Normalization::None)),
        "base64url" => return Ok(preset("base64url", BASE64URL, Normalization::None)),
        _ => {}
    }

    // SPEC §3.2: a custom alphabet that ASCII-case-insensitively equals a
    // preset name is almost certainly a misspelled preset — accepting it
    // would silently build a codec over the letters of the name.
    let lower = alphabet.to_ascii_lowercase();
    if PRESET_NAMES.contains(&lower.as_str()) {
        return Err(format!(
            "custom alphabet {alphabet:?} matches the preset name {lower:?} — \
             pass {lower:?} for the preset, or a genuinely custom alphabet"
        ));
    }

    // Custom alphabet: 2-94 distinct printable ASCII characters (0x21-0x7E).
    let count = alphabet.chars().count();
    if !(2..=94).contains(&count) {
        return Err("custom alphabet must be 2-94 characters".to_owned());
    }
    let mut seen = [false; 128];
    for c in alphabet.chars() {
        if !('\x21'..='\x7e').contains(&c) {
            return Err("custom alphabet must be printable ASCII (0x21-0x7E)".to_owned());
        }
        if std::mem::replace(&mut seen[c as usize], true) {
            return Err("custom alphabet characters must be distinct".to_owned());
        }
    }
    Ok(ResolvedAlphabet {
        name: None,
        chars: alphabet.to_owned(),
        normalization: Normalization::None,
    })
}
