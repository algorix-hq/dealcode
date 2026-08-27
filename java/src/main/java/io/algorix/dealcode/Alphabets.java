package io.algorix.dealcode;

/**
 * The preset alphabet strings defined by SPEC §3.1.
 *
 * <p>Each constant is the exact, ordered character sequence of a preset; the
 * character at index {@code i} represents numeral value {@code i}. Pass the
 * preset <em>name</em> (for example {@code "hex"} or {@code "crockford"}) to
 * {@link Dealcode.Builder#alphabet(String)} to get the preset together with
 * its decode normalization; passing one of these constant <em>strings</em>
 * instead configures the same characters as a custom alphabet, which has no
 * normalization.</p>
 */
public final class Alphabets {

    /** Preset {@code "dec"} — radix 10, no decode normalization. */
    public static final String DEC = "0123456789";

    /** Preset {@code "hex"} — radix 16; decode ASCII-lowercases input. */
    public static final String HEX = "0123456789abcdef";

    /** Preset {@code "base32"} (RFC 4648) — radix 32; decode ASCII-uppercases input. */
    public static final String BASE32 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

    /**
     * Preset {@code "crockford"} (Crockford Base32) — radix 32; decode
     * ASCII-uppercases input, then maps {@code O→0}, {@code I→1}, {@code L→1}.
     */
    public static final String CROCKFORD = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

    /** Preset {@code "base36"} — radix 36; decode ASCII-lowercases input. */
    public static final String BASE36 = "0123456789abcdefghijklmnopqrstuvwxyz";

    /** Preset {@code "base58"} (Bitcoin) — radix 58, no decode normalization. */
    public static final String BASE58 =
            "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

    /** Preset {@code "base62"} — radix 62, no decode normalization. */
    public static final String BASE62 =
            "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

    /** Preset {@code "base64url"} (RFC 4648 §5) — radix 64, no decode normalization. */
    public static final String BASE64URL =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

    private Alphabets() {
        // static constants only
    }
}
