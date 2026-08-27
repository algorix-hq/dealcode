package io.algorix.dealcode;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import org.junit.jupiter.api.Test;

/**
 * Regression tests for the QA round-2 findings: absurd lengths must fail in
 * O(1), and string inputs with U+0000 or unpaired surrogates must be rejected
 * rather than silently encoded as '?' (SPEC §2, §2.1).
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
}
