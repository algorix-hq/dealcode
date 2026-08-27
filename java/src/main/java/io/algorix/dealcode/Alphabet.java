package io.algorix.dealcode;

/**
 * Alphabet presets and normalization rules (SPEC §3). Internal.
 */
final class Alphabet {

    /** Decode-input normalization kinds. All are ASCII-only (SPEC §3.1). */
    enum Norm {
        /** No normalization; input must match the alphabet exactly. */
        NONE,
        /** Map only {@code A-Z} to {@code a-z}; leave everything else alone. */
        ASCII_LOWER,
        /** Map only {@code a-z} to {@code A-Z}; leave everything else alone. */
        ASCII_UPPER,
        /** ASCII-uppercase, then map {@code O→0}, {@code I→1}, {@code L→1}. */
        CROCKFORD
    }

    /** Preset name, or {@code null} for a custom alphabet. */
    final String name;
    /** The ordered characters; index = numeral value. */
    final String chars;
    private final Norm norm;

    private Alphabet(String name, String chars, Norm norm) {
        this.name = name;
        this.chars = chars;
        this.norm = norm;
    }

    /**
     * Resolves a preset name or a custom alphabet string (SPEC §3.1, §3.2).
     * Preset names win on conflict.
     *
     * @throws ConfigException if the string is neither a preset name nor a
     *     valid custom alphabet
     */
    static Alphabet resolve(String alphabet) {
        if (alphabet == null) {
            throw new ConfigException("alphabet must not be null");
        }
        switch (alphabet) {
            case "dec":
                return new Alphabet("dec", Alphabets.DEC, Norm.NONE);
            case "hex":
                return new Alphabet("hex", Alphabets.HEX, Norm.ASCII_LOWER);
            case "base32":
                return new Alphabet("base32", Alphabets.BASE32, Norm.ASCII_UPPER);
            case "crockford":
                return new Alphabet("crockford", Alphabets.CROCKFORD, Norm.CROCKFORD);
            case "base36":
                return new Alphabet("base36", Alphabets.BASE36, Norm.ASCII_LOWER);
            case "base58":
                return new Alphabet("base58", Alphabets.BASE58, Norm.NONE);
            case "base62":
                return new Alphabet("base62", Alphabets.BASE62, Norm.NONE);
            case "base64url":
                return new Alphabet("base64url", Alphabets.BASE64URL, Norm.NONE);
            default:
                return custom(alphabet);
        }
    }

    private static Alphabet custom(String alphabet) {
        int len = alphabet.length();
        if (len < 2 || len > 94) {
            throw new ConfigException(
                    "custom alphabet must have 2 to 94 characters, got " + len
                            + " (or use a preset name: dec, hex, base32, crockford,"
                            + " base36, base58, base62, base64url)");
        }
        boolean[] seen = new boolean[128];
        for (int i = 0; i < len; i++) {
            char c = alphabet.charAt(i);
            if (c < 0x21 || c > 0x7E) {
                throw new ConfigException(
                        "custom alphabet must contain only printable ASCII (0x21-0x7E);"
                                + " found U+" + String.format("%04X", (int) c)
                                + " at index " + i);
            }
            if (seen[c]) {
                throw new ConfigException(
                        "custom alphabet characters must be distinct; '" + c
                                + "' appears more than once");
            }
            seen[c] = true;
        }
        return new Alphabet(null, alphabet, Norm.NONE);
    }

    /**
     * Applies this alphabet's decode normalization (SPEC §3.1). ASCII-only:
     * never locale-sensitive, never touches characters outside {@code A-Z} /
     * {@code a-z} (plus the Crockford {@code O/I/L} mapping).
     */
    String normalize(String s) {
        switch (norm) {
            case NONE:
                return s;
            case ASCII_LOWER:
                return asciiLower(s);
            case ASCII_UPPER:
                return asciiUpper(s);
            case CROCKFORD:
                return crockford(s);
            default:
                throw new AssertionError(norm);
        }
    }

    private static String asciiLower(String s) {
        char[] out = null;
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            if (c >= 'A' && c <= 'Z') {
                if (out == null) {
                    out = s.toCharArray();
                }
                out[i] = (char) (c + 32);
            }
        }
        return out == null ? s : new String(out);
    }

    private static String asciiUpper(String s) {
        char[] out = null;
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            if (c >= 'a' && c <= 'z') {
                if (out == null) {
                    out = s.toCharArray();
                }
                out[i] = (char) (c - 32);
            }
        }
        return out == null ? s : new String(out);
    }

    private static String crockford(String s) {
        char[] out = null;
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            if (c >= 'a' && c <= 'z') {
                c = (char) (c - 32);
            }
            if (c == 'O') {
                c = '0';
            } else if (c == 'I' || c == 'L') {
                c = '1';
            }
            if (c != s.charAt(i)) {
                if (out == null) {
                    out = s.toCharArray();
                }
                out[i] = c;
            }
        }
        return out == null ? s : new String(out);
    }
}
