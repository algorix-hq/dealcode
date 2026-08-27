package io.algorix.dealcode;

import java.math.BigInteger;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;

/**
 * Collision-free, random-looking codes from a counter (dealcode format
 * version 1).
 *
 * <p>A {@code Dealcode} codec maps a non-negative integer counter {@code n}
 * (from a database sequence or any other source that never repeats) to a
 * short, fixed-alphabet, random-looking string called a <em>code</em>, and
 * back. The mapping is a bijection (a keyed FF1 permutation per NIST
 * SP 800-38G), so two different counters can never produce the same code, and
 * anyone holding the key can decode a code back to its counter. Without the
 * key, codes carry no usable order or volume information.</p>
 *
 * <pre>{@code
 * Dealcode codec = Dealcode.builder()
 *         .key(System.getenv("DEALCODE_KEY"))
 *         .domain("orders")
 *         .build();
 *
 * String code = codec.encode(42);   // e.g. "a06e33"
 * long n = codec.decode(code);      // 42
 * }</pre>
 *
 * <p>Codes start at {@link #minLength()} characters and grow one character at
 * a time only when the current length is exhausted, up to
 * {@link #maxLength()}. Encodable counters are exactly
 * {@code [0, maxCounter()]}.</p>
 *
 * <p><b>Immutability rule.</b> For a given code namespace (one counter
 * sequence) the entire configuration — key, alphabet, lengths, domain — must
 * never change once codes have been issued; changing any of it creates an
 * unrelated permutation whose outputs may collide with already-issued
 * codes.</p>
 *
 * <p><b>Thread safety.</b> Instances are immutable and safe for concurrent
 * use without synchronization: the only stateful component, the AES
 * {@link javax.crypto.Cipher}, is held per-thread in a {@link ThreadLocal}.
 * Create one codec per code namespace at startup and share it freely.</p>
 *
 * @see <a href="https://github.com/algorix/dealcode">dealcode specification (SPEC.md)</a>
 */
public final class Dealcode {

    /** Counters live in [0, min(radix^maxLength, 2^63)). */
    private static final BigInteger COUNTER_BOUND = BigInteger.ONE.shiftLeft(63);
    /** radix^maxLength must not exceed 2^128. */
    private static final BigInteger CODESPACE_BOUND = BigInteger.ONE.shiftLeft(128);
    private static final String TWEAK_PREFIX = "dealcode/v1/";
    private static final byte[] KDF_PREFIX =
            "dealcode/v1/kdf".getBytes(StandardCharsets.US_ASCII);

    private final Alphabet alphabet;
    private final int radix;
    private final int minLength;
    private final int maxLength;
    private final String domain;
    private final FF1 ff1;
    /** FF1 round parameters, indexed by {@code length - minLength}. */
    private final FF1.Params[] params;
    /** char -> numeral for chars < 128; -1 means "not in alphabet". */
    private final int[] charIndex;
    /** powers[d] = radix^d for d in [0, maxLength]. */
    private final BigInteger[] powers;
    /** stageUpper[d] = radix^d when it fits a signed long (stageHuge[d] false). */
    private final long[] stageUpper;
    private final boolean[] stageHuge;
    /** min(radix^maxLength, 2^63) - 1; always fits a signed long. */
    private final long maxCounter;

    private Dealcode(byte[] aesKey, Alphabet alphabet, int minLength, int maxLength,
            String domain) {
        this.alphabet = alphabet;
        this.radix = alphabet.chars.length();
        this.minLength = minLength;
        this.maxLength = maxLength;
        this.domain = domain;
        this.ff1 = new FF1(aesKey, radix);

        byte[] tweak = (TWEAK_PREFIX + domain).getBytes(StandardCharsets.UTF_8);
        this.params = new FF1.Params[maxLength - minLength + 1];
        for (int d = minLength; d <= maxLength; d++) {
            this.params[d - minLength] = ff1.params(tweak, d);
        }

        this.charIndex = new int[128];
        java.util.Arrays.fill(charIndex, -1);
        for (int i = 0; i < radix; i++) {
            charIndex[alphabet.chars.charAt(i)] = i;
        }

        BigInteger r = BigInteger.valueOf(radix);
        this.powers = new BigInteger[maxLength + 1];
        this.stageUpper = new long[maxLength + 1];
        this.stageHuge = new boolean[maxLength + 1];
        BigInteger pow = BigInteger.ONE;
        for (int d = 0; d <= maxLength; d++) {
            powers[d] = pow;
            if (pow.bitLength() <= 63) {
                stageUpper[d] = pow.longValueExact();
                stageHuge[d] = false;
            } else {
                stageUpper[d] = Long.MAX_VALUE;
                stageHuge[d] = true;
            }
            if (d < maxLength) {
                pow = pow.multiply(r);
            }
        }
        this.maxCounter = powers[maxLength].min(COUNTER_BOUND)
                .subtract(BigInteger.ONE).longValueExact();
    }

    /**
     * Returns a new {@link Builder}. A key is required; every other option
     * has a spec-defined default.
     *
     * @return a fresh builder
     */
    public static Builder builder() {
        return new Builder();
    }

    // -- public API ------------------------------------------------------------

    /**
     * Maps counter {@code n} to its code (SPEC §5).
     *
     * <p>O(1) in the counter value: ten AES rounds. The returned code's length
     * is between {@link #minLength()} and {@link #maxLength()}, determined
     * purely by which length stage {@code n} falls in.</p>
     *
     * @param n the counter; must be in {@code [0, maxCounter()]}
     * @return the code for {@code n}, rendered in this codec's alphabet
     * @throws CounterRangeException if {@code n} is negative or exceeds
     *     {@link #maxCounter()}
     */
    public String encode(long n) {
        if (n < 0 || n > maxCounter) {
            throw new CounterRangeException(
                    "counter " + n + " out of range [0, " + maxCounter + "]");
        }
        int d = minLength;
        while (!stageHuge[d] && n >= stageUpper[d]) {
            d++;
        }
        long base = (d == minLength) ? 0L : stageUpper[d - 1];
        long v = n - base;
        int[] x = new int[d];
        for (int i = d - 1; i >= 0; i--) {
            x[i] = (int) (v % radix);
            v /= radix;
        }
        int[] y = ff1.encrypt(params[d - minLength], x);
        char[] out = new char[d];
        for (int i = 0; i < d; i++) {
            out[i] = alphabet.chars.charAt(y[i]);
        }
        return new String(out);
    }

    /**
     * Maps a code back to its counter (SPEC §7).
     *
     * <p>The alphabet's decode normalization (SPEC §3.1) is applied first —
     * e.g. the {@code hex} preset accepts uppercase input, {@code crockford}
     * maps {@code O→0}, {@code I→1}, {@code L→1}. Custom alphabets have no
     * normalization: input must match exactly.</p>
     *
     * <p>Decode success only proves the code is <em>consistent</em> with the
     * key — the application still decides whether counter {@code n} actually
     * exists (e.g. by looking it up).</p>
     *
     * @param code the code string
     * @return the counter that produces this code
     * @throws InvalidCodeException if the code fails the length, character-set,
     *     or stage/counter-range checks — i.e. this codec never issued it
     */
    public long decode(String code) {
        if (code == null) {
            throw new InvalidCodeException("code must not be null");
        }
        String s = alphabet.normalize(code);
        int d = s.length();
        if (d < minLength || d > maxLength) {
            throw new InvalidCodeException(
                    "code length " + d + " outside [" + minLength + ", " + maxLength + "]");
        }
        int[] y = new int[d];
        for (int i = 0; i < d; i++) {
            char c = s.charAt(i);
            int numeral = (c < 128) ? charIndex[c] : -1;
            if (numeral < 0) {
                throw new InvalidCodeException("character '" + c + "' not in alphabet");
            }
            y[i] = numeral;
        }
        int[] x = ff1.decrypt(params[d - minLength], y);
        BigInteger r = BigInteger.valueOf(radix);
        BigInteger v = BigInteger.ZERO;
        for (int xi : x) {
            v = v.multiply(r).add(BigInteger.valueOf(xi));
        }
        BigInteger base = (d == minLength) ? BigInteger.ZERO : powers[d - 1];
        if (d > minLength && v.compareTo(powers[d].subtract(base)) >= 0) {
            throw new InvalidCodeException("code was not issued by this codec");
        }
        BigInteger n = base.add(v);
        if (n.compareTo(COUNTER_BOUND) >= 0) {
            throw new InvalidCodeException("code was not issued by this codec");
        }
        return n.longValueExact();
    }

    // -- introspection -----------------------------------------------------------

    /**
     * Returns the alphabet's canonical characters, in numeral order (the
     * character at index {@code i} represents numeral value {@code i}).
     *
     * @return the alphabet string
     */
    public String alphabet() {
        return alphabet.chars;
    }

    /**
     * Returns the radix — the number of characters in the alphabet.
     *
     * @return the radix
     */
    public int radix() {
        return radix;
    }

    /**
     * Returns the minimum (initial) code length.
     *
     * @return the minimum code length
     */
    public int minLength() {
        return minLength;
    }

    /**
     * Returns the maximum code length.
     *
     * @return the maximum code length
     */
    public int maxLength() {
        return maxLength;
    }

    /**
     * Returns the domain — the namespace label bound into the FF1 tweak.
     * Two codecs with the same key but different domains produce unrelated
     * permutations.
     *
     * @return the domain string (possibly empty)
     */
    public String domain() {
        return domain;
    }

    /**
     * Returns the largest encodable counter:
     * {@code min(radix^maxLength, 2^63) − 1}.
     *
     * <p>Encodable counters are exactly {@code [0, maxCounter()]}. This is
     * expressed as an inclusive maximum rather than a capacity because the
     * capacity may be exactly 2<sup>63</sup> (e.g. 16-character hex codes),
     * which does not fit in a signed {@code long}; the maximum always
     * does.</p>
     *
     * @return the largest counter accepted by {@link #encode(long)}
     */
    public long maxCounter() {
        return maxCounter;
    }

    /** Returns a description of this codec. The key never appears here. */
    @Override
    public String toString() {
        String name = (alphabet.name != null) ? alphabet.name : "custom(" + radix + ")";
        return "Dealcode(alphabet=" + name + ", minLength=" + minLength
                + ", maxLength=" + maxLength + ", domain=\"" + domain + "\")";
    }

    // -- builder ---------------------------------------------------------------

    /**
     * Builder for {@link Dealcode} codecs.
     *
     * <p>Obtain via {@link Dealcode#builder()}. A key is required; all other
     * options default per the spec: alphabet {@code "hex"}, {@code minLength}
     * 6, {@code maxLength} the largest length whose full code space fits in a
     * signed 64-bit counter, and an empty domain.</p>
     *
     * <p>Builders are mutable and not thread-safe; the {@link Dealcode}
     * instances they produce are immutable and thread-safe.</p>
     */
    public static final class Builder {

        private byte[] keyBytes;
        private String keyString;
        private boolean keySet;
        private String alphabet = "hex";
        private int minLength = 6;
        private Integer maxLength; // null = spec default, computed at build()
        private String domain = "";

        private Builder() {
        }

        /**
         * Sets the key from raw bytes (SPEC §2.1). Bytes of length exactly
         * 16, 24, or 32 are used directly as the AES key; any other non-empty
         * length is deterministically expanded to an AES-256 key via
         * {@code SHA-256("dealcode/v1/kdf" || material)}. The array is copied.
         *
         * @param key the key material; must be non-empty
         * @return this builder
         * @throws ConfigException if {@code key} is null
         */
        public Builder key(byte[] key) {
            if (key == null) {
                throw new ConfigException("key must not be null");
            }
            this.keyBytes = key.clone();
            this.keyString = null;
            this.keySet = true;
            return this;
        }

        /**
         * Sets the key from a string (SPEC §2.1). Strings are <em>always</em>
         * derived — the UTF-8 bytes are expanded to an AES-256 key via
         * {@code SHA-256("dealcode/v1/kdf" || material)} regardless of length
         * or content; a hex-looking string is not auto-decoded. A passphrase
         * key is exactly as strong as the passphrase; prefer at least 128 bits
         * of random material (e.g. {@code openssl rand -hex 32} output).
         *
         * @param key the key material; must be non-empty
         * @return this builder
         * @throws ConfigException if {@code key} is null
         */
        public Builder key(String key) {
            if (key == null) {
                throw new ConfigException("key must not be null");
            }
            this.keyString = key;
            this.keyBytes = null;
            this.keySet = true;
            return this;
        }

        /**
         * Sets the alphabet: a preset name ({@code "dec"}, {@code "hex"},
         * {@code "base32"}, {@code "crockford"}, {@code "base36"},
         * {@code "base58"}, {@code "base62"}, {@code "base64url"}) or a custom
         * alphabet string of 2–94 distinct printable ASCII characters
         * (0x21–0x7E). Preset names win on conflict. Custom alphabets have no
         * decode normalization. Default: {@code "hex"}.
         *
         * @param alphabet a preset name or custom alphabet string
         * @return this builder
         * @throws ConfigException if {@code alphabet} is null
         */
        public Builder alphabet(String alphabet) {
            if (alphabet == null) {
                throw new ConfigException("alphabet must not be null");
            }
            this.alphabet = alphabet;
            return this;
        }

        /**
         * Sets the minimum (initial) code length. Must be at least 2 and
         * satisfy {@code radix^minLength >= 100} (FF1's structural minimum
         * domain size). Default: 6. Set {@code minLength == maxLength} for
         * fixed-length codes.
         *
         * @param minLength the minimum code length
         * @return this builder
         */
        public Builder minLength(int minLength) {
            this.minLength = minLength;
            return this;
        }

        /**
         * Sets the maximum code length. Must be at least {@code minLength}
         * and satisfy {@code radix^maxLength <= 2^128}. Default: the largest
         * {@code L} with {@code radix^L <= 2^63 − 1} (hex → 15, dec → 18,
         * base32/crockford → 12, base58/base62/base64url → 10), but never
         * below {@code minLength}.
         *
         * @param maxLength the maximum code length
         * @return this builder
         */
        public Builder maxLength(int maxLength) {
            this.maxLength = maxLength;
            return this;
        }

        /**
         * Sets the domain — an application-chosen namespace label (e.g.
         * {@code "orders"}, {@code "coupons"}) bound into the FF1 tweak as
         * {@code "dealcode/v1/" + domain}. Same key, different domain →
         * unrelated codes. At most 255 UTF-8 bytes. Default: {@code ""}.
         *
         * @param domain the namespace label
         * @return this builder
         * @throws ConfigException if {@code domain} is null
         */
        public Builder domain(String domain) {
            if (domain == null) {
                throw new ConfigException("domain must not be null");
            }
            this.domain = domain;
            return this;
        }

        /**
         * Validates the configuration (SPEC §2) and builds an immutable,
         * thread-safe codec.
         *
         * @return the codec
         * @throws ConfigException if no key was set, the key is empty, the
         *     alphabet is invalid, the lengths violate
         *     {@code minLength >= 2}, {@code radix^minLength >= 100},
         *     {@code minLength <= maxLength}, or
         *     {@code radix^maxLength <= 2^128}, or the domain exceeds 255
         *     UTF-8 bytes
         */
        public Dealcode build() {
            if (!keySet) {
                throw new ConfigException("key is required");
            }
            byte[] aesKey = resolveKey(keyBytes, keyString);
            Alphabet alpha = Alphabet.resolve(alphabet);
            int radix = alpha.chars.length();
            BigInteger r = BigInteger.valueOf(radix);

            // 128 is the structural maximum (radix >= 2, radix^maxLength
            // <= 2^128); checked before any pow so absurd lengths fail in O(1).
            if (minLength < 2 || minLength > 128) {
                throw new ConfigException("minLength must be in [2, 128], got " + minLength);
            }
            // radix >= 2, so radix^minLength >= 2^7 = 128 >= 100 whenever
            // minLength >= 7; only small minLength needs the exact check.
            if (minLength < 7 && r.pow(minLength).compareTo(BigInteger.valueOf(100)) < 0) {
                throw new ConfigException(
                        "radix^minLength must be at least 100 (FF1 minimum domain): "
                                + radix + "^" + minLength + " = " + r.pow(minLength));
            }
            int max = (maxLength != null) ? maxLength : defaultMaxLength(r, minLength);
            if (max < minLength) {
                throw new ConfigException(
                        "maxLength must be >= minLength (" + minLength + "), got " + max);
            }
            // radix^maxLength <= 2^128 implies maxLength <= 128 (radix >= 2);
            // check cheaply first so absurd lengths fail without a giant pow.
            if (max > 128 || r.pow(max).compareTo(CODESPACE_BOUND) > 0) {
                throw new ConfigException(
                        "radix^maxLength must not exceed 2^128: " + radix + "^" + max);
            }
            requireCleanUnicode(domain, "domain");
            if (domain.getBytes(StandardCharsets.UTF_8).length > 255) {
                throw new ConfigException("domain must be at most 255 UTF-8 bytes");
            }
            return new Dealcode(aesKey, alpha, minLength, max, domain);
        }

        /**
         * Rejects U+0000 and unpaired surrogates (SPEC §2.1) — silently
         * encoding them (Java would emit '?') makes the "same" input produce
         * different permutations across languages.
         */
        private static void requireCleanUnicode(String value, String what) {
            if (value.indexOf('\u0000') >= 0) {
                throw new ConfigException(what + " must not contain U+0000");
            }
            for (int i = 0; i < value.length(); i++) {
                char c = value.charAt(i);
                if (Character.isHighSurrogate(c)) {
                    if (i + 1 >= value.length() || !Character.isLowSurrogate(value.charAt(i + 1))) {
                        throw new ConfigException(
                                what + " must be valid Unicode (no unpaired surrogates)");
                    }
                    i++;
                } else if (Character.isLowSurrogate(c)) {
                    throw new ConfigException(
                            what + " must be valid Unicode (no unpaired surrogates)");
                }
            }
        }

        /** Key material handling (SPEC §2.1). */
        private static byte[] resolveKey(byte[] keyBytes, String keyString) {
            byte[] material;
            boolean direct;
            if (keyString != null) {
                requireCleanUnicode(keyString, "string key material");
                material = keyString.getBytes(StandardCharsets.UTF_8);
                direct = false; // strings are always derived
            } else {
                material = keyBytes;
                int len = material.length;
                direct = (len == 16 || len == 24 || len == 32);
            }
            if (material.length == 0) {
                throw new ConfigException("key must not be empty");
            }
            if (direct) {
                return material;
            }
            try {
                MessageDigest sha256 = MessageDigest.getInstance("SHA-256");
                sha256.update(KDF_PREFIX);
                return sha256.digest(material);
            } catch (NoSuchAlgorithmException e) {
                throw new IllegalStateException("SHA-256 unavailable", e); // unreachable
            }
        }

        /**
         * The largest L >= minLength with radix^L <= 2^63 − 1; never below
         * minLength (SPEC §2, mirroring the reference implementation).
         */
        private static int defaultMaxLength(BigInteger radix, int minLength) {
            int length = minLength;
            BigInteger cap = radix.pow(minLength);
            while (cap.multiply(radix).compareTo(COUNTER_BOUND) < 0) {
                cap = cap.multiply(radix);
                length++;
            }
            return length;
        }
    }
}
