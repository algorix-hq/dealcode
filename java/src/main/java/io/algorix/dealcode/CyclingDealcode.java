package io.algorix.dealcode;

import java.math.BigInteger;
import java.nio.charset.StandardCharsets;

/**
 * Fixed-length codes that refill the same space cycle after cycle (dealcode
 * fixed-length cycling mode, SPEC §11, tweak namespace {@code dealcode/v1c/}).
 *
 * <p>Codes are always exactly {@link #length()} characters — they never grow.
 * The 63-bit counter space is used in <em>cycles</em> of
 * {@code capacity = radix^length} counters each: counter {@code n} belongs to
 * cycle {@code n / capacity} and maps to in-cycle value {@code n % capacity}.
 * Every cycle is a different keyed FF1 permutation of the same fixed-length
 * code space (a different FF1 tweak, {@code "dealcode/v1c/" + cycle + "/" +
 * domain}), so when the space is exhausted it refills in a new,
 * unpredictable order instead of growing — airline-PNR-style codes.</p>
 *
 * <pre>{@code
 * CyclingDealcode codec = CyclingDealcode.builder()
 *         .key(System.getenv("DEALCODE_KEY"))
 *         .alphabet("crockford")
 *         .length(6)
 *         .domain("bookings")
 *         .build();
 *
 * String code = codec.encode(n);                // always 6 chars
 * long cycle = codec.cycleOf(n);                // persist this with the code
 * long back = codec.decode(code, cycle);        // == n
 * }</pre>
 *
 * <p><b>Codes repeat across cycles — by design.</b> Within one cycle every
 * code is unique (each of the {@code capacity} possible strings is issued
 * exactly once), but the next cycle issues the <em>same</em> strings again in
 * a different order (pigeonhole: the space is being refilled). This mode is
 * only sound when at most one cycle's codes are <em>live</em> at a time in a
 * given uniqueness scope: retire or expire cycle {@code e}'s codes before
 * issuing from cycle {@code e + 1}, or scope storage by cycle. A global
 * {@code UNIQUE(code)} index spanning cycles WILL fire — use
 * {@code UNIQUE(cycle, code)} or equivalent. The application must persist
 * which cycle each live code belongs to (or the currently active cycle):
 * {@link #decode(String, long)} requires it, because a code string alone is
 * ambiguous across cycles and the library cannot recover the cycle from
 * it.</p>
 *
 * <p><b>Immutability rule.</b> As with {@link Dealcode}, the entire
 * configuration — key, alphabet, length, domain — must never change for a
 * code namespace once codes have been issued.</p>
 *
 * <p><b>Thread safety.</b> Instances are immutable and safe for concurrent
 * use without synchronization: the only stateful component, the AES
 * {@link javax.crypto.Cipher}, is held per-thread in a {@link ThreadLocal}.
 * Create one codec per code namespace at startup and share it freely.</p>
 *
 * @see Dealcode variable-length mode (codes grow instead of cycling)
 * @see <a href="https://github.com/algorix-hq/dealcode">dealcode specification (SPEC.md §11)</a>
 */
public final class CyclingDealcode {

    /** Counters live in [0, 2^63). */
    private static final BigInteger COUNTER_BOUND = BigInteger.ONE.shiftLeft(63);
    private static final String TWEAK_PREFIX = "dealcode/v1c/";

    private final Alphabet alphabet;
    private final int radix;
    private final int length;
    private final String domain;
    private final FF1 ff1;
    /** char -> numeral for chars < 128; -1 means "not in alphabet". */
    private final int[] charIndex;
    /**
     * Per-cycle capacity radix^length, exact. May be exactly 2^63 (e.g. the
     * octal/length-21 configuration), which does not fit a signed
     * {@code long} — so the capacity is held as a {@link BigInteger} plus the
     * long-math view below.
     */
    private final BigInteger capacity;
    /**
     * True iff capacity == 2^63 exactly. In that case there is a single
     * cycle (cycle 0), every counter is its own in-cycle value, and no
     * {@code long} division by the capacity is needed — all remaining
     * arithmetic stays in plain non-negative {@code long}s.
     */
    private final boolean fullCapacity;
    /** capacity as a long; only meaningful when {@code !fullCapacity}. */
    private final long capacityLong;
    /** (2^63 - 1) / capacity; always fits a signed long. */
    private final long maxCycle;

    private CyclingDealcode(byte[] aesKey, Alphabet alphabet, int length,
            String domain, BigInteger capacity) {
        this.alphabet = alphabet;
        this.radix = alphabet.chars.length();
        this.length = length;
        this.domain = domain;
        this.ff1 = new FF1(aesKey, radix);
        this.capacity = capacity;
        this.fullCapacity = capacity.equals(COUNTER_BOUND);
        this.capacityLong = fullCapacity ? Long.MAX_VALUE /* unused */
                : capacity.longValueExact();
        this.maxCycle = fullCapacity ? 0L : Long.MAX_VALUE / capacityLong;

        this.charIndex = new int[128];
        java.util.Arrays.fill(charIndex, -1);
        for (int i = 0; i < radix; i++) {
            charIndex[alphabet.chars.charAt(i)] = i;
        }
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
     * Maps counter {@code n} to its fixed-length code (SPEC §11.2).
     *
     * <p>The counter's cycle is implied: {@code n} encodes as in-cycle value
     * {@code n % capacity} under the tweak of cycle {@code n / capacity}
     * (i.e. {@link #cycleOf(long)}). O(1) in the counter value: ten AES
     * rounds. The returned code is always exactly {@link #length()}
     * characters.</p>
     *
     * @param n the counter; any non-negative {@code long}
     * @return the code for {@code n}, rendered in this codec's alphabet
     * @throws CounterRangeException if {@code n} is negative
     */
    public String encode(long n) {
        checkCounter(n);
        long cycle;
        long v;
        if (fullCapacity) {
            cycle = 0L; // single cycle: capacity == 2^63 covers all counters
            v = n;
        } else {
            cycle = n / capacityLong;
            v = n % capacityLong;
        }
        int[] x = new int[length];
        long rest = v;
        for (int i = length - 1; i >= 0; i--) {
            x[i] = (int) (rest % radix);
            rest /= radix;
        }
        int[] y = ff1.encrypt(paramsFor(cycle), x);
        char[] out = new char[length];
        for (int i = 0; i < length; i++) {
            out[i] = alphabet.chars.charAt(y[i]);
        }
        return new String(out);
    }

    /**
     * Maps a code issued in {@code cycle} back to its counter (SPEC §11.2).
     *
     * <p>The cycle is required: the same code string recurs in every cycle,
     * mapping to a different counter each time — a code alone is ambiguous by
     * design. Store the cycle alongside each live code (or the currently
     * active cycle) and pass it here.</p>
     *
     * <p>The alphabet's decode normalization (SPEC §3.1) is applied first —
     * e.g. the {@code hex} preset accepts uppercase input, {@code crockford}
     * maps {@code O→0}, {@code I→1}, {@code L→1}. Custom alphabets have no
     * normalization: input must match exactly.</p>
     *
     * <p>Decode success only proves the code is <em>consistent</em> with the
     * key and cycle — the application still decides whether counter {@code n}
     * actually exists. Decoding a valid code under the <em>wrong</em> cycle
     * does not fail; it returns a different counter.</p>
     *
     * @param code the code string; must be exactly {@link #length()} characters
     * @param cycle the cycle the code was issued in; must be in
     *     {@code [0, maxCycle()]}
     * @return the counter that produces this code in this cycle
     * @throws CounterRangeException if {@code cycle} is negative or exceeds
     *     {@link #maxCycle()}
     * @throws InvalidCodeException if the code fails the length,
     *     character-set, or counter-bound checks — i.e. this codec never
     *     issued it in this cycle
     */
    public long decode(String code, long cycle) {
        if (cycle < 0 || cycle > maxCycle) {
            throw new CounterRangeException(
                    "cycle " + cycle + " out of range [0, " + maxCycle + "]");
        }
        if (code == null) {
            throw new InvalidCodeException("code must not be null");
        }
        // Fixed-length gate before normalization: normalization is
        // length-preserving, so this is behaviour-identical, and it keeps
        // rejection of oversized garbage O(1) instead of normalizing
        // megabytes first (SPEC §7 via §11.2). The gate counts code points
        // when that is cheap so the message matches the other
        // implementations; valid codes are ASCII, where the counts agree.
        int gate = code.length() <= 4 * length
                ? code.codePointCount(0, code.length())
                : code.length();
        if (gate != length) {
            throw new InvalidCodeException(
                    "code length " + gate + " != " + length + " (fixed-length mode)");
        }
        String s = alphabet.normalize(code);
        int[] y = new int[length];
        for (int i = 0; i < length; i++) {
            char c = s.charAt(i);
            int numeral = (c < 128) ? charIndex[c] : -1;
            if (numeral < 0) {
                throw new InvalidCodeException("character '" + c + "' not in alphabet");
            }
            y[i] = numeral;
        }
        int[] x = ff1.decrypt(paramsFor(cycle), y);
        // v < radix^length = capacity <= 2^63, and every intermediate value
        // is a base-radix prefix of the final one (hence <= it), so plain
        // non-negative long accumulation cannot overflow.
        long v = 0;
        for (int xi : x) {
            v = v * radix + xi;
        }
        if (fullCapacity) {
            return v; // cycle == 0 and v < 2^63: always in range
        }
        // cycle <= maxCycle = (2^63 - 1) / capacity, so cycle * capacity
        // <= 2^63 - 1: the multiplication cannot overflow. Only the final
        // addition can push past 2^63 - 1, and only in the last, partial
        // cycle — those code values were never issued.
        long base = cycle * capacityLong;
        if (v > Long.MAX_VALUE - base) {
            throw new InvalidCodeException("code was not issued in this cycle");
        }
        return base + v;
    }

    /**
     * Returns the cycle that counter {@code n} belongs to:
     * {@code n / capacity}, i.e. the cycle {@link #encode(long)} implies and
     * {@link #decode(String, long)} later needs.
     *
     * @param n the counter; any non-negative {@code long}
     * @return the cycle of {@code n}, in {@code [0, maxCycle()]}
     * @throws CounterRangeException if {@code n} is negative
     */
    public long cycleOf(long n) {
        checkCounter(n);
        return fullCapacity ? 0L : n / capacityLong;
    }

    // -- introspection -----------------------------------------------------------

    /**
     * Returns the number of codes per cycle: {@code radix^length}, exact.
     *
     * <p>Returned as a {@link BigInteger} because the capacity may be exactly
     * 2<sup>63</sup> (e.g. an 8-character alphabet with length 21 — the
     * largest configuration this mode accepts), which does not fit in a
     * signed {@code long}; every smaller capacity does, and
     * {@link BigInteger#longValueExact()} recovers it when the caller knows
     * its configuration is below the limit.</p>
     *
     * @return {@code radix^length} as an exact {@link BigInteger}
     */
    public BigInteger capacity() {
        return capacity;
    }

    /**
     * Returns the largest usable cycle: {@code (2^63 - 1) / capacity}
     * (0 when the capacity is exactly 2<sup>63</sup>). Cycles run
     * {@code [0, maxCycle()]}; the last one is partial unless the capacity
     * divides 2<sup>63</sup> exactly.
     *
     * @return the largest cycle accepted by {@link #decode(String, long)}
     */
    public long maxCycle() {
        return maxCycle;
    }

    /**
     * Returns the fixed code length — every code is exactly this many
     * characters.
     *
     * @return the code length
     */
    public int length() {
        return length;
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
     * Returns the alphabet's canonical characters, in numeral order (the
     * character at index {@code i} represents numeral value {@code i}).
     *
     * @return the alphabet string
     */
    public String alphabet() {
        return alphabet.chars;
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

    /** Returns a description of this codec. The key never appears here. */
    @Override
    public String toString() {
        String name = (alphabet.name != null) ? alphabet.name : "custom(" + radix + ")";
        return "CyclingDealcode(alphabet=" + name + ", length=" + length
                + ", domain=\"" + domain + "\")";
    }

    // -- helpers ---------------------------------------------------------------

    private static void checkCounter(long n) {
        if (n < 0) {
            throw new CounterRangeException(
                    "counter " + n + " out of range [0, " + Long.MAX_VALUE + "]");
        }
    }

    /**
     * FF1 round parameters for one cycle's tweak,
     * {@code "dealcode/v1c/" + cycle + "/" + domain} in UTF-8 (SPEC §11.2).
     * {@link Long#toString(long)} renders the cycle in base 10 with no
     * leading zeros ({@code "0"} for cycle zero), so with the domain capped
     * at 255 bytes the tweak is at most 288 bytes.
     */
    private FF1.Params paramsFor(long cycle) {
        byte[] tweak = (TWEAK_PREFIX + cycle + "/" + domain)
                .getBytes(StandardCharsets.UTF_8);
        return ff1.params(tweak, length);
    }

    // -- builder ---------------------------------------------------------------

    /**
     * Builder for {@link CyclingDealcode} codecs.
     *
     * <p>Obtain via {@link CyclingDealcode#builder()}. A key is required; all
     * other options default per the spec: alphabet {@code "hex"},
     * {@code length} 6, and an empty domain. Key and alphabet follow exactly
     * the same rules and misuse guards as {@link Dealcode.Builder}.</p>
     *
     * <p>Builders are mutable and not thread-safe; the
     * {@link CyclingDealcode} instances they produce are immutable and
     * thread-safe.</p>
     */
    public static final class Builder {

        private byte[] keyBytes;
        private String keyString;
        private boolean keySet;
        private String alphabet = "hex";
        private int length = 6;
        private String domain = "";

        private Builder() {
        }

        /**
         * Sets the key from raw bytes (SPEC §2.1, shared with plain v1).
         * Bytes of length exactly 16, 24, or 32 are used directly as the AES
         * key; any other non-empty length is deterministically expanded to an
         * AES-256 key via {@code SHA-256("dealcode/v1/kdf" || material)}. The
         * array is copied.
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
         * Sets the key from a string (SPEC §2.1, shared with plain v1).
         * Strings are <em>always</em> derived — the UTF-8 bytes are expanded
         * to an AES-256 key via
         * {@code SHA-256("dealcode/v1/kdf" || material)} regardless of length
         * or content; a hex-looking string is not auto-decoded.
         *
         * <p>A string key whose ASCII-lowercase equals a preset alphabet name
         * (for example {@code "crockford"}) is rejected at {@link #build()} —
         * it almost certainly means the key and alphabet arguments were
         * swapped. Byte-array keys are unaffected.</p>
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
         * {@code "base58"}, {@code "base62"}, {@code "base64url"}) or a
         * custom alphabet string of 2–94 distinct printable ASCII characters
         * (0x21–0x7E), with the same rules and miscased-preset guard as
         * {@link Dealcode.Builder#alphabet(String)}. Default: {@code "hex"}.
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
         * Sets the fixed code length {@code L}. Must be in {@code [2, 128]}
         * and satisfy {@code 100 <= radix^L <= 2^63}: the per-cycle capacity
         * {@code radix^L} must itself fit the 63-bit counter space so a cycle
         * can complete — for larger fixed code spaces use {@link Dealcode}
         * with {@code minLength == maxLength}. Default: 6.
         *
         * @param length the fixed code length
         * @return this builder
         */
        public Builder length(int length) {
            this.length = length;
            return this;
        }

        /**
         * Sets the domain — an application-chosen namespace label (e.g.
         * {@code "bookings"}) bound into each cycle's FF1 tweak as
         * {@code "dealcode/v1c/" + cycle + "/" + domain}. Same key, different
         * domain → unrelated codes; the {@code dealcode/v1c/} prefix also
         * keeps cycling-mode tweaks disjoint from every plain-v1 tweak. At
         * most 255 UTF-8 bytes. Default: {@code ""}.
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
         * Validates the configuration (SPEC §11.1) and builds an immutable,
         * thread-safe codec.
         *
         * @return the codec
         * @throws ConfigException if no key was set, the key is empty or a
         *     preset alphabet name, the alphabet is invalid, {@code length}
         *     is outside {@code [2, 128]}, {@code radix^length} is below 100
         *     or above 2^63, or the domain exceeds 255 UTF-8 bytes
         */
        public CyclingDealcode build() {
            if (!keySet) {
                throw new ConfigException("key is required");
            }
            byte[] aesKey = Dealcode.Builder.resolveKey(keyBytes, keyString);
            Alphabet alpha = Alphabet.resolve(alphabet);
            int radix = alpha.chars.length();

            // Bound the length BEFORE computing any power (SPEC §11.1); the
            // pow below is then O(1)-small.
            if (length < 2 || length > 128) {
                throw new ConfigException("length must be in [2, 128], got " + length);
            }
            BigInteger cap = BigInteger.valueOf(radix).pow(length);
            if (cap.compareTo(BigInteger.valueOf(100)) < 0) {
                throw new ConfigException(
                        "radix^length must be at least 100 (FF1 minimum domain): "
                                + radix + "^" + length + " = " + cap);
            }
            // The capacity check is done in BigInteger: radix^length may be
            // exactly 2^63 (legal — a single full cycle) which a signed long
            // cannot hold.
            if (cap.compareTo(COUNTER_BOUND) > 0) {
                throw new ConfigException(
                        "radix^length must not exceed 2^63 in cycling mode ("
                                + radix + "^" + length + ") — a cycle must be"
                                + " completable; use Dealcode with minLength =="
                                + " maxLength for larger fixed spaces");
            }
            Dealcode.Builder.requireCleanUnicode(domain, "domain");
            if (domain.getBytes(StandardCharsets.UTF_8).length > 255) {
                throw new ConfigException("domain must be at most 255 UTF-8 bytes");
            }
            return new CyclingDealcode(aesKey, alpha, length, domain, cap);
        }
    }
}
