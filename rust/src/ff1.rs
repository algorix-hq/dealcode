//! FF1 format-preserving encryption (NIST SP 800-38G, Algorithms 7 and 8).
//!
//! Implemented directly from the NIST specification and validated against the
//! official NIST FF1-AES sample vectors (`testvectors/ff1_nist.json`).
//!
//! This module is internal: numeral strings are `&[u8]` slices of values in
//! `[0, radix)` (dealcode caps the radix at 94, so `u8` numerals suffice), and
//! all callers go through configuration validation that guarantees the
//! arithmetic below fits in `u128` (`radix^v < 2^96`, `d ≤ 16`).

use aes::cipher::{BlockEncrypt, KeyInit};
use aes::{Aes128, Aes192, Aes256, Block};

/// AES-ECB block encryption for whichever key size the codec was built with.
///
/// Round keys are expanded once at construction; `encrypt_block` takes `&self`
/// so a single cipher is freely shared across threads.
// The variants intentionally mirror the AES key-size names.
#[allow(clippy::enum_variant_names)]
#[derive(Clone)]
pub(crate) enum AesCipher {
    Aes128(Aes128),
    Aes192(Aes192),
    Aes256(Aes256),
}

impl AesCipher {
    /// Builds a cipher from a 16-, 24-, or 32-byte AES key.
    ///
    /// The key length is validated by the caller (key resolution only ever
    /// produces those three sizes).
    pub(crate) fn new(key: &[u8]) -> Self {
        match key.len() {
            16 => AesCipher::Aes128(Aes128::new(key.into())),
            24 => AesCipher::Aes192(Aes192::new(key.into())),
            32 => AesCipher::Aes256(Aes256::new(key.into())),
            // Key resolution (SPEC §2.1) only produces 16/24/32-byte keys.
            n => unreachable!("AES key must be 16, 24, or 32 bytes, got {n}"),
        }
    }

    fn encrypt_block(&self, block: &mut [u8; 16]) {
        let block = Block::from_mut_slice(block);
        match self {
            AesCipher::Aes128(c) => c.encrypt_block(block),
            AesCipher::Aes192(c) => c.encrypt_block(block),
            AesCipher::Aes256(c) => c.encrypt_block(block),
        }
    }

    /// `PRF(X)` from SP 800-38G: CBC-MAC with a zero IV, built by chaining
    /// ECB block encryptions. `data.len()` is always a multiple of 16 by
    /// construction of `Q`.
    fn prf(&self, data: &[u8]) -> [u8; 16] {
        debug_assert_eq!(data.len() % 16, 0, "PRF input must be whole blocks");
        let mut acc = [0u8; 16];
        for block in data.chunks_exact(16) {
            for (a, b) in acc.iter_mut().zip(block) {
                *a ^= b;
            }
            self.encrypt_block(&mut acc);
        }
        acc
    }
}

/// Precomputed per-(tweak, length) FF1 parameters (step 1-4 of Algorithms
/// 7/8, plus the constant prefix of `Q`).
#[derive(Clone)]
pub(crate) struct Params {
    /// Length of the left half.
    u: usize,
    /// Length of the right half.
    v: usize,
    /// Byte length of `NUM(B)` in `Q`.
    b: usize,
    /// Byte length of the round output `S`.
    d: usize,
    /// `P ‖ tweak ‖ zero padding` — everything in `P ‖ Q` before the round
    /// number byte.
    q_prefix: Vec<u8>,
    /// `radix^u`.
    mod_u: u128,
    /// `radix^v`.
    mod_v: u128,
}

/// Computes FF1 parameters for numeral strings of length `n` under `tweak`.
///
/// Panics if `radix^n < 100`, `n < 2`, or the configuration exceeds the
/// bounds under which `u128` arithmetic is exact (`d ≤ 16`); dealcode codec
/// construction validates all of these before this is ever called.
pub(crate) fn params(radix: u32, tweak: &[u8], n: usize) -> Params {
    let r = u128::from(radix);
    assert!((2..=65536).contains(&radix), "radix out of range [2, 2^16]");
    assert!(n >= 2, "FF1 message too short");
    // radix^n >= 100 (FF1's structural minimum domain); None means overflow,
    // which is certainly >= 100.
    assert!(
        r.checked_pow(n as u32).map_or(true, |x| x >= 100),
        "FF1 domain radix^n must be at least 100"
    );

    let u = n / 2;
    let v = n - u;
    let mod_v = r
        .checked_pow(v as u32)
        .expect("radix^v fits in u128 (guaranteed by radix^max_length <= 2^128)");
    let mod_u = r
        .checked_pow(u as u32)
        .expect("radix^u fits in u128 (u <= v)");

    // b = ceil(ceil(v * log2(radix)) / 8), computed exactly as the bit
    // length of radix^v - 1 (never via floating-point logarithms).
    let b = (128 - (mod_v - 1).leading_zeros() as usize).div_ceil(8);
    let d = 4 * b.div_ceil(4) + 4;
    assert!(d <= 16, "S expansion exceeds 128 bits (unreachable within validated configurations)");

    let t = tweak.len();
    let pad = (16 - (t + b + 1) % 16) % 16;
    let mut q_prefix = Vec::with_capacity(16 + t + pad);
    // P = [1]1 ‖ [2]1 ‖ [1]1 ‖ [radix]3 ‖ [10]1 ‖ [u mod 256]1 ‖ [n]4 ‖ [t]4
    q_prefix.extend_from_slice(&[
        1,
        2,
        1,
        (radix >> 16) as u8,
        (radix >> 8) as u8,
        radix as u8,
        10,
        u as u8,
    ]);
    q_prefix.extend_from_slice(&(n as u32).to_be_bytes());
    q_prefix.extend_from_slice(&(t as u32).to_be_bytes());
    q_prefix.extend_from_slice(tweak);
    q_prefix.resize(q_prefix.len() + pad, 0);

    Params { u, v, b, d, q_prefix, mod_u, mod_v }
}

/// `NUM_radix(xs)`: numeral string to integer, big-endian.
pub(crate) fn num(radix: u128, xs: &[u8]) -> u128 {
    // Callers guarantee radix^len(xs) <= 2^128, so the fold cannot overflow:
    // the accumulator stays < radix^len(xs) - 1 <= 2^128 - 1.
    xs.iter()
        .fold(0u128, |acc, &x| acc.wrapping_mul(radix).wrapping_add(u128::from(x)))
}

/// `STR^len_radix(value)`: integer to a zero-padded numeral string.
fn str_radix(radix: u128, mut value: u128, len: usize) -> Vec<u8> {
    let mut out = vec![0u8; len];
    for slot in out.iter_mut().rev() {
        *slot = (value % radix) as u8;
        value /= radix;
    }
    out
}

/// One round's `y`: `PRF(P ‖ Q)` expanded to `d` bytes and read big-endian.
///
/// The general `S = R ‖ CIPH(R ⊕ [1]16) ‖ CIPH(R ⊕ [2]16) ‖ …` expansion loop
/// is implemented even though `d ≤ 16` never needs it under dealcode's
/// bounds.
fn round_y(cipher: &AesCipher, p: &Params, round: u8, num_val: u128) -> u128 {
    let mut data = Vec::with_capacity(p.q_prefix.len() + 16);
    data.extend_from_slice(&p.q_prefix);
    data.push(round);
    data.extend_from_slice(&num_val.to_be_bytes()[16 - p.b..]);
    let r = cipher.prf(&data);

    let mut s = r.to_vec();
    let mut j: u128 = 1;
    while s.len() < p.d {
        let jb = j.to_be_bytes();
        let mut block = [0u8; 16];
        for (out, (a, b)) in block.iter_mut().zip(r.iter().zip(jb.iter())) {
            *out = a ^ b;
        }
        cipher.encrypt_block(&mut block);
        s.extend_from_slice(&block);
        j += 1;
    }

    s[..p.d]
        .iter()
        .fold(0u128, |acc, &byte| (acc << 8) | u128::from(byte))
}

/// `FF1.Encrypt` (Algorithm 7). `x` is a numeral string; returns one of the
/// same length.
pub(crate) fn encrypt(cipher: &AesCipher, p: &Params, radix: u32, x: &[u8]) -> Vec<u8> {
    let r = u128::from(radix);
    let mut a = x[..p.u].to_vec();
    let mut b_half = x[p.u..].to_vec();
    for i in 0..10u8 {
        let y = round_y(cipher, p, i, num(r, &b_half));
        let (m, modulus) = if i % 2 == 0 { (p.u, p.mod_u) } else { (p.v, p.mod_v) };
        // Reduce y before adding: both operands are < modulus <= 2^96, so the
        // u128 addition cannot overflow.
        let c = (num(r, &a) + y % modulus) % modulus;
        a = std::mem::replace(&mut b_half, str_radix(r, c, m));
    }
    a.extend_from_slice(&b_half);
    a
}

/// `FF1.Decrypt` (Algorithm 8). Inverse of [`encrypt`].
pub(crate) fn decrypt(cipher: &AesCipher, p: &Params, radix: u32, x: &[u8]) -> Vec<u8> {
    let r = u128::from(radix);
    let mut a = x[..p.u].to_vec();
    let mut b_half = x[p.u..].to_vec();
    for i in (0..10u8).rev() {
        let y = round_y(cipher, p, i, num(r, &a));
        let (m, modulus) = if i % 2 == 0 { (p.u, p.mod_u) } else { (p.v, p.mod_v) };
        // Modular subtraction without signed types or overflow: every term
        // is < modulus <= 2^96.
        let c = (num(r, &b_half) + (modulus - y % modulus)) % modulus;
        b_half = std::mem::replace(&mut a, str_radix(r, c, m));
    }
    a.extend_from_slice(&b_half);
    a
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde::Deserialize;

    /// The NIST sample alphabet: character index = numeral value.
    const NIST_ALPHABET: &str = "0123456789abcdefghijklmnopqrstuvwxyz";

    #[derive(Deserialize)]
    struct File {
        vectors: Vec<Vector>,
    }

    #[derive(Deserialize)]
    struct Vector {
        sample: u32,
        cipher: String,
        key_hex: String,
        radix: u32,
        tweak_hex: String,
        plaintext: String,
        ciphertext: String,
    }

    fn unhex(s: &str) -> Vec<u8> {
        assert!(s.len() % 2 == 0);
        (0..s.len())
            .step_by(2)
            .map(|i| u8::from_str_radix(&s[i..i + 2], 16).unwrap())
            .collect()
    }

    fn to_numerals(s: &str) -> Vec<u8> {
        s.chars()
            .map(|c| NIST_ALPHABET.find(c).expect("char in NIST alphabet") as u8)
            .collect()
    }

    fn to_string(xs: &[u8]) -> String {
        xs.iter()
            .map(|&x| NIST_ALPHABET.as_bytes()[x as usize] as char)
            .collect()
    }

    /// Every official NIST FF1-AES sample, encrypt and decrypt directions.
    #[test]
    fn nist_ff1_sample_vectors() {
        let path = concat!(env!("CARGO_MANIFEST_DIR"), "/../testvectors/ff1_nist.json");
        let file: File =
            serde_json::from_str(&std::fs::read_to_string(path).expect("read ff1_nist.json"))
                .expect("parse ff1_nist.json");
        assert_eq!(file.vectors.len(), 9);

        for v in &file.vectors {
            let key = unhex(&v.key_hex);
            let tweak = unhex(&v.tweak_hex);
            let cipher = AesCipher::new(&key);
            let p = params(v.radix, &tweak, v.plaintext.chars().count());

            let pt = to_numerals(&v.plaintext);
            let ct = to_numerals(&v.ciphertext);

            let got_ct = encrypt(&cipher, &p, v.radix, &pt);
            assert_eq!(
                to_string(&got_ct),
                v.ciphertext,
                "sample {} ({}) encrypt",
                v.sample,
                v.cipher
            );

            let got_pt = decrypt(&cipher, &p, v.radix, &ct);
            assert_eq!(
                to_string(&got_pt),
                v.plaintext,
                "sample {} ({}) decrypt",
                v.sample,
                v.cipher
            );
        }
    }

    /// Encrypt/decrypt must be inverses for assorted lengths and radices.
    #[test]
    fn ff1_roundtrip() {
        let cipher = AesCipher::new(&[7u8; 16]);
        for &(radix, n) in &[(10u32, 6usize), (16, 16), (36, 19), (62, 12), (94, 4), (2, 20)] {
            let p = params(radix, b"dealcode/v1/test", n);
            for seed in 0u128..50 {
                let x: Vec<u8> = (0..n)
                    .map(|i| ((seed.wrapping_mul(2654435761) as usize + i * 7) % radix as usize) as u8)
                    .collect();
                let y = encrypt(&cipher, &p, radix, &x);
                assert_eq!(y.len(), n);
                assert_eq!(decrypt(&cipher, &p, radix, &y), x);
            }
        }
    }
}
