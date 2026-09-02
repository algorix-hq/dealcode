package io.algorix.dealcode;

import java.math.BigInteger;
import java.nio.charset.StandardCharsets;

/**
 * Integer codes drawn without repetition from {@code [low, high]} (dealcode
 * integer range mode, SPEC §12, tweak namespace {@code dealcode/v1r/}).
 *
 * <p>Counters {@code 0 <= n < capacity} map bijectively to integer codes in
 * {@code [low, low + capacity - 1]} through a single FF1 call — no loops, no
 * cycle-walking. {@code capacity} is the largest FF1 domain
 * ({@code radix^m} with {@code radix <= 256}) that fits in the range, so it
 * can be slightly smaller than {@code high - low + 1}; the uncovered top
 * slice (the <em>dead zone</em>) is never issued and is rejected by
 * {@link #decode(long)}.</p>
 *
 * <p>Built for ranges like 100000–999999: every code is a 6-digit integer
 * with no leading zero, safe to store in an integer column and to round-trip
 * through spreadsheets and other systems that would strip a leading zero
 * from a string code.</p>
 *
 * <pre>{@code
 * RangeDealcode codec = RangeDealcode.builder()
 *         .key(System.getenv("DEALCODE_KEY"))
 *         .low(100_000)
 *         .high(999_999)
 *         .build();
 *
 * codec.capacity();                 // 96^3 = 884736 — 98.3% of the range
 * long code = codec.encode(0);      // an int in [100000, 984735]
 * long back = codec.decode(code);   // == 0
 * }</pre>
 *
 * <p><b>Effective capacity is {@code capacity()}, not the range span.</b>
 * Counters run {@code [0, capacity)}; when they are exhausted, they are
 * exhausted — this mode has no staging and no cycles. Applications needing
 * the top of the range exactly should widen {@code high} so the capacity
 * covers what they need (SPEC §12.2, §12.4).</p>
 *
 * <p><b>Immutability rule.</b> As with {@link Dealcode}, the entire
 * configuration — key, {@code low}, {@code high}, domain — must never change
 * for a code namespace once codes have been issued.</p>
 *
 * <p><b>Thread safety.</b> Instances are immutable and safe for concurrent
 * use without synchronization: the only stateful component, the AES
 * {@link javax.crypto.Cipher}, is held per-thread in a {@link ThreadLocal}.
 * Create one codec per code namespace at startup and share it freely.</p>
 *
 * @see Dealcode variable-length string mode
 * @see CyclingDealcode fixed-length cycling string mode
 * @see <a href="https://github.com/algorix-hq/dealcode">dealcode specification (SPEC.md §12)</a>
 */
public final class RangeDealcode {

    /** low/high live in [0, 2^63); capacity may be exactly 2^63. */
    private static final BigInteger COUNTER_BOUND = BigInteger.ONE.shiftLeft(63);
    private static final String TWEAK_PREFIX = "dealcode/v1r/";
    /** Numerals stay one byte in every FF1 core (SPEC §12.2). */
    private static final int MAX_RADIX = 256;
    /** m <= 63: radix >= 2, so radix^m <= 2^63 forces m <= 63 (SPEC §12.2). */
    private static final int MAX_M = 63;

    private final long low;
    private final long high;
    private final String domain;
    private final int radix;
    private final int m;
    private final FF1 ff1;
    /**
     * Round parameters for the mode's single tweak,
     * {@code "dealcode/v1r/" + low + "/" + high + "/" + domain} in UTF-8
     * (SPEC §12.3). Unlike the other modes the tweak never varies, so the
     * parameters are computed once here; {@link FF1.Params} is read-only
     * during encrypt/decrypt, so sharing it across threads is safe.
     */
    private final FF1.Params params;
    /**
     * The derived capacity radix^m, exact. May be exactly 2^63 (e.g. the
     * full-counter-space range {@code [0, 2^63 - 1]}, where 128^9 = 2^63),
     * which does not fit a signed {@code long} — so the capacity is held as
     * a {@link BigInteger} plus the long-math view below.
     */
    private final BigInteger capacity;
    /**
     * True iff capacity == 2^63 exactly. In that case every non-negative
     * {@code long} counter is encodable, {@code low} is necessarily 0, and
     * no {@code long} comparison against the capacity is needed — all
     * remaining arithmetic stays in plain non-negative {@code long}s.
     */
    private final boolean fullCapacity;
    /** capacity as a long; only meaningful when {@code !fullCapacity}. */
    private final long capacityLong;

    private RangeDealcode(byte[] aesKey, long low, long high, String domain) {
        this.low = low;
        this.high = high;
        this.domain = domain;

        BigInteger span = BigInteger.valueOf(high)
                .subtract(BigInteger.valueOf(low)).add(BigInteger.ONE);
        int[] selected = selectDomain(span);
        this.radix = selected[0];
        this.m = selected[1];
        this.capacity = BigInteger.valueOf(radix).pow(m);
        this.fullCapacity = capacity.equals(COUNTER_BOUND);
        this.capacityLong = fullCapacity ? Long.MAX_VALUE /* unused */
                : capacity.longValueExact();

        this.ff1 = new FF1(aesKey, radix);
        byte[] tweak = (TWEAK_PREFIX + low + "/" + high + "/" + domain)
                .getBytes(StandardCharsets.UTF_8);
        this.params = ff1.params(tweak, m);
    }

    /**
     * Returns a new {@link Builder}. A key, {@code low}, and {@code high}
     * are required; the domain defaults to {@code ""}.
     *
     * @return a fresh builder
     */
    public static Builder builder() {
        return new Builder();
    }

    // -- public API ------------------------------------------------------------

    /**
     * Maps counter {@code n} to its integer code in
     * {@code [low, low + capacity - 1]} (SPEC §12.3).
     *
     * <p>O(1) in the counter value: ten AES rounds. Distinct counters give
     * distinct codes — uniqueness is structural.</p>
     *
     * @param n the counter; must be in {@code [0, capacity())}
     * @return the code for {@code n}, an integer in
     *     {@code [low, low + capacity - 1]}
     * @throws CounterRangeException if {@code n} is negative or not below
     *     {@link #capacity()}
     */
    public long encode(long n) {
        if (n < 0 || (!fullCapacity && n >= capacityLong)) {
            throw new CounterRangeException(
                    "counter " + n + " out of range [0, " + capacity + ")");
        }
        int[] y = ff1.encrypt(params, toNumerals(n));
        // v < radix^m = capacity <= 2^63, and every intermediate value is a
        // base-radix prefix of the final one (hence <= it), so plain
        // non-negative long accumulation cannot overflow; low + v <=
        // low + capacity - 1 <= high <= 2^63 - 1, so neither can the sum.
        long v = 0;
        for (int yi : y) {
            v = v * radix + yi;
        }
        return low + v;
    }

    /**
     * Maps integer {@code code} back to its counter (SPEC §12.3).
     *
     * <p>Decode success only proves the code is <em>consistent</em> with the
     * key and range — the application still decides whether counter
     * {@code n} actually exists.</p>
     *
     * @param code the code; must be in {@code [low, low + capacity - 1]}
     * @return the counter that produces this code
     * @throws InvalidCodeException if {@code code} is outside
     *     {@code [low, high]} or falls in the dead zone
     *     {@code [low + capacity, high]} — i.e. this codec never issued it
     */
    public long decode(long code) {
        if (code < low || code > high) {
            throw new InvalidCodeException(
                    "code " + code + " outside range [" + low + ", " + high + "]");
        }
        long v = code - low;
        if (!fullCapacity && v >= capacityLong) {
            throw new InvalidCodeException(
                    "code " + code + " in the unissued top slice of the range"
                            + " (capacity " + capacity + ")");
        }
        int[] x = ff1.decrypt(params, toNumerals(v));
        // n < capacity always holds (FF1 permutes [0, radix^m)), so no
        // further range check is needed; the accumulation cannot overflow,
        // by the same prefix argument as in encode.
        long n = 0;
        for (int xi : x) {
            n = n * radix + xi;
        }
        return n;
    }

    // -- introspection -----------------------------------------------------------

    /**
     * Returns the lower bound of the range — the smallest issuable code.
     *
     * @return the configured {@code low}
     */
    public long low() {
        return low;
    }

    /**
     * Returns the upper bound of the range. Codes above
     * {@code low + capacity - 1} are never issued (the dead zone), but the
     * configured bound is part of the permutation's identity: it is bound
     * into the FF1 tweak.
     *
     * @return the configured {@code high}
     */
    public long high() {
        return high;
    }

    /**
     * Returns the number of issuable codes: the largest
     * {@code radix^m <= high - low + 1} with {@code radix <= 256}
     * (SPEC §12.2). Counters run {@code [0, capacity)} and codes run
     * {@code [low, low + capacity - 1]}.
     *
     * <p>Returned as a {@link BigInteger} because the capacity may be exactly
     * 2<sup>63</sup> (the full-counter-space range {@code [0, 2^63 - 1]}),
     * which does not fit in a signed {@code long}; every smaller capacity
     * does, and {@link BigInteger#longValueExact()} recovers it when the
     * caller knows its configuration is below the limit.</p>
     *
     * @return {@code radix^m} as an exact {@link BigInteger}
     */
    public BigInteger capacity() {
        return capacity;
    }

    /**
     * Returns the internal FF1 radix (SPEC §12.2); informational.
     *
     * @return the derived radix, in {@code [2, 256]}
     */
    public int radix() {
        return radix;
    }

    /**
     * Returns the domain — the namespace label bound into the FF1 tweak.
     * Two codecs with the same key but different domains produce unrelated
     * permutations, exactly as different ranges do.
     *
     * @return the domain string (possibly empty)
     */
    public String domain() {
        return domain;
    }

    /** Returns a description of this codec. The key never appears here. */
    @Override
    public String toString() {
        return "RangeDealcode(low=" + low + ", high=" + high
                + ", domain=\"" + domain + "\")";
    }

    // -- domain selection (SPEC §12.2) ------------------------------------------

    /**
     * Selects the largest FF1 domain {@code radix^m <= span} with
     * {@code radix} in {@code [2, 256]} and {@code m} in {@code [2, 63]};
     * among equal capacities the smallest {@code m} wins (strict {@code >}
     * below). Exact integer arithmetic throughout — {@code span} may be 2^63
     * and candidate powers approach it, so the search runs in
     * {@link BigInteger}; floating-point roots MUST NOT be used.
     *
     * @return {@code {radix, m}} of the winning domain
     */
    private static int[] selectDomain(BigInteger span) {
        BigInteger bestCapacity = BigInteger.ZERO;
        int bestRadix = 0;
        int bestM = 0;
        for (int m = 2; m <= MAX_M; m++) {
            int r = cappedRoot(span, m);
            if (r < 2) {
                continue;
            }
            BigInteger c = BigInteger.valueOf(r).pow(m); // c <= span by construction
            if (c.compareTo(bestCapacity) > 0) {
                bestCapacity = c;
                bestRadix = r;
                bestM = m;
            }
        }
        // Always found: m = 2 gives r = min(floor(sqrt(span)), 256) >= 10
        // for span >= 100 (guaranteed by build()).
        return new int[] {bestRadix, bestM};
    }

    /**
     * {@code min(iroot(n, m), 256)}: the largest {@code r <= }{@link
     * #MAX_RADIX} with {@code r^m <= n}, by binary search with exact powers
     * (SPEC §12.2 — floating-point roots MUST NOT be used). Folding the
     * radix cap into the search bounds keeps every candidate power
     * O(1)-small; the search runs once per constructed codec. Returns 0 when
     * even {@code 1^m > n} (i.e. {@code n < 1}).
     */
    private static int cappedRoot(BigInteger n, int m) {
        int lo = 1;
        int hi = MAX_RADIX;
        int best = 0;
        while (lo <= hi) {
            int mid = (lo + hi) >>> 1;
            if (BigInteger.valueOf(mid).pow(m).compareTo(n) <= 0) {
                best = mid;
                lo = mid + 1;
            } else {
                hi = mid - 1;
            }
        }
        return best;
    }

    // -- helpers ---------------------------------------------------------------

    /** {@code value} as {@code m} big-endian base-{@code radix} numerals. */
    private int[] toNumerals(long value) {
        int[] out = new int[m];
        long rest = value;
        for (int i = m - 1; i >= 0; i--) {
            out[i] = (int) (rest % radix);
            rest /= radix;
        }
        return out;
    }

    // -- builder ---------------------------------------------------------------

    /**
     * Builder for {@link RangeDealcode} codecs.
     *
     * <p>Obtain via {@link RangeDealcode#builder()}. A key, {@link #low(long)},
     * and {@link #high(long)} are required; the domain defaults to {@code ""}.
     * Keys follow exactly the same rules and misuse guards as
     * {@link Dealcode.Builder}.</p>
     *
     * <p>Builders are mutable and not thread-safe; the {@link RangeDealcode}
     * instances they produce are immutable and thread-safe.</p>
     */
    public static final class Builder {

        private byte[] keyBytes;
        private String keyString;
        private boolean keySet;
        private long low;
        private boolean lowSet;
        private long high;
        private boolean highSet;
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
         * it almost certainly means the arguments were mixed up. Byte-array
         * keys are unaffected.</p>
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
         * Sets the lower bound of the range — the smallest issuable code.
         * Must satisfy {@code 0 <= low <= high} (SPEC §12.1). Required.
         *
         * @param low the lower bound, inclusive
         * @return this builder
         */
        public Builder low(long low) {
            this.low = low;
            this.lowSet = true;
            return this;
        }

        /**
         * Sets the upper bound of the range, inclusive. Must satisfy
         * {@code low <= high} with a span {@code high - low + 1} of at least
         * 100 values (FF1 structural minimum, SPEC §12.1). Required.
         *
         * @param high the upper bound, inclusive
         * @return this builder
         */
        public Builder high(long high) {
            this.high = high;
            this.highSet = true;
            return this;
        }

        /**
         * Sets the domain — an application-chosen namespace label (e.g.
         * {@code "bookings"}) bound into the FF1 tweak as
         * {@code "dealcode/v1r/" + low + "/" + high + "/" + domain}. Same
         * key, different domain → unrelated codes; the {@code dealcode/v1r/}
         * prefix also keeps range-mode tweaks disjoint from every plain-v1
         * and cycling-mode tweak. At most 255 UTF-8 bytes. Default:
         * {@code ""}.
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
         * Validates the configuration (SPEC §12.1) and builds an immutable,
         * thread-safe codec.
         *
         * @return the codec
         * @throws ConfigException if no key was set, the key is empty or a
         *     preset alphabet name, {@code low} or {@code high} was not set,
         *     {@code low} is negative or above {@code high}, the range spans
         *     fewer than 100 values, or the domain exceeds 255 UTF-8 bytes
         */
        public RangeDealcode build() {
            if (!keySet) {
                throw new ConfigException("key is required");
            }
            byte[] aesKey = Dealcode.Builder.resolveKey(keyBytes, keyString);
            if (!lowSet || !highSet) {
                throw new ConfigException("low and high are required");
            }
            // high <= 2^63 - 1 is enforced by the long type itself.
            if (low < 0 || low > high) {
                throw new ConfigException(
                        "low/high must satisfy 0 <= low <= high <= 2^63 - 1,"
                                + " got low=" + low + ", high=" + high);
            }
            // The span high - low + 1 may be exactly 2^63, which overflows a
            // signed long — compare without computing it: span < 100 iff
            // high - low < 99, and high - low never overflows here.
            if (high - low < 99) {
                throw new ConfigException(
                        "range must span at least 100 values (FF1 minimum domain)");
            }
            Dealcode.Builder.requireCleanUnicode(domain, "domain");
            if (domain.getBytes(StandardCharsets.UTF_8).length > 255) {
                throw new ConfigException("domain must be at most 255 UTF-8 bytes");
            }
            return new RangeDealcode(aesKey, low, high, domain);
        }
    }
}
