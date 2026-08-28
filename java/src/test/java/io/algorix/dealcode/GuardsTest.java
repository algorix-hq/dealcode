package io.algorix.dealcode;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.nio.charset.StandardCharsets;
import org.junit.jupiter.api.Test;

/**
 * Regression tests for the QA round-2 findings: absurd lengths must fail in
 * O(1), and string inputs with U+0000 or unpaired surrogates must be rejected
 * rather than silently encoded as '?' (SPEC §2, §2.1). Plus the miscased
 * preset-name guards: a custom alphabet that is a miscased preset name, and a
 * string key that is a preset name (swapped arguments), are both rejected.
 */
class GuardsTest {

    @Test
    void absurdLengthsRejectedFast() {
        long t0 = System.nanoTime();
        assertThrows(ConfigException.class,
                () -> Dealcode.builder().key("k").maxLength(Integer.MAX_VALUE).build());
        assertThrows(ConfigException.class,
                () -> Dealcode.builder().key("k").minLength(1_000_000_000).build());
        long ms = (System.nanoTime() - t0) / 1_000_000;
        assertTrue(ms < 100, "absurd lengths took " + ms + "ms; must fail in O(1)");
    }

    @Test
    void nulAndLoneSurrogatesRejected() {
        assertThrows(ConfigException.class,
                () -> Dealcode.builder().key("k").domain("a\u0000b").build());
        assertThrows(ConfigException.class,
                () -> Dealcode.builder().key("a\u0000b").build());
        assertThrows(ConfigException.class,
                () -> Dealcode.builder().key("k").domain("\ud800").build());
        assertThrows(ConfigException.class,
                () -> Dealcode.builder().key("k").domain("\udfff").build());
        assertThrows(ConfigException.class,
                () -> Dealcode.builder().key("\ud800").build());
    }

    @Test
    void legitimateUnicodeStillWorks() {
        Dealcode codec = Dealcode.builder().key("k").domain("한국어-✅-😀").build();
        assertEquals(42L, codec.decode(codec.encode(42L)));
    }

    @Test
    void miscasedPresetNameAsCustomAlphabetRejected() {
        ConfigException e = assertThrows(ConfigException.class,
                () -> Dealcode.builder().key("k").alphabet("HEX").build());
        assertEquals("custom alphabet \"HEX\" matches the preset name \"hex\""
                + " — pass \"hex\" for the preset, or a genuinely custom alphabet",
                e.getMessage());
        // Every miscasing whose ASCII-lowercase is a preset name is rejected.
        assertThrows(ConfigException.class,
                () -> Dealcode.builder().key("k").alphabet("Crockford").build());
        assertThrows(ConfigException.class,
                () -> Dealcode.builder().key("k").alphabet("Base64URL").build());
        assertThrows(ConfigException.class,
                () -> Dealcode.builder().key("k").alphabet("DEC").build());
    }

    @Test
    void exactPresetNameStillResolvesAsPreset() {
        Dealcode codec = Dealcode.builder().key("k").alphabet("hex").build();
        assertEquals(Alphabets.HEX, codec.alphabet());
        assertEquals(42L, codec.decode(codec.encode(42L)));
    }

    @Test
    void genuinelyCustomAlphabetsUnaffectedByPresetNameGuard() {
        // Preset *characters* are not preset *names* — still a valid custom
        // alphabet (with no decode normalization).
        Dealcode chars = Dealcode.builder().key("k").alphabet(Alphabets.HEX).build();
        assertEquals(42L, chars.decode(chars.encode(42L)));
        // An alphabet that merely contains letters of a preset name is fine.
        Dealcode custom = Dealcode.builder().key("k").alphabet("HEXA").build();
        assertEquals(42L, custom.decode(custom.encode(42L)));
    }

    @Test
    void stringKeyThatIsPresetNameRejected() {
        ConfigException e = assertThrows(ConfigException.class,
                () -> Dealcode.builder().key("crockford").build());
        assertEquals("string key \"crockford\" is a preset alphabet name"
                + " — did you swap the key and alphabet arguments?",
                e.getMessage());
        // The guard compares ASCII-lowercase, so miscasings are caught too.
        assertThrows(ConfigException.class,
                () -> Dealcode.builder().key("HEX").alphabet("base32").build());
        assertThrows(ConfigException.class,
                () -> Dealcode.builder().key("Base64url").build());
    }

    @Test
    void byteKeysUnaffectedByPresetNameGuard() {
        Dealcode codec = Dealcode.builder()
                .key("crockford".getBytes(StandardCharsets.US_ASCII))
                .build();
        assertEquals(42L, codec.decode(codec.encode(42L)));
    }

    @Test
    void nonPresetStringKeysStillWork() {
        Dealcode codec = Dealcode.builder().key("crockford2").build();
        assertEquals(42L, codec.decode(codec.encode(42L)));
    }
}
