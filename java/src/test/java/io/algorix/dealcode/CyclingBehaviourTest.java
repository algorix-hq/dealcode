package io.algorix.dealcode;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

import java.math.BigInteger;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import org.junit.jupiter.api.Nested;
import org.junit.jupiter.api.Test;

/** Behavioural tests for cycling mode beyond the shared spec vectors. */
class CyclingBehaviourTest {

    private static final byte[] KEY_16 = new byte[] {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
    };

    @Nested
    class Cycles {

        @Test
        void everyCycleIsAPermutationOfTheSameSpaceInADifferentOrder() {
            CyclingDealcode codec = CyclingDealcode.builder()
                    .key("k").alphabet("dec").length(2).build(); // capacity 100
            assertEquals(BigInteger.valueOf(100), codec.capacity());

            List<List<String>> cycles = new ArrayList<>();
            for (long e = 0; e < 3; e++) {
                List<String> codes = new ArrayList<>();
                for (long v = 0; v < 100; v++) {
                    long n = e * 100 + v;
                    assertEquals(e, codec.cycleOf(n));
                    String code = codec.encode(n);
                    assertEquals(2, code.length());
                    assertEquals(n, codec.decode(code, e), "roundtrip of " + n);
                    codes.add(code);
                }
                assertEquals(100, Set.copyOf(codes).size(),
                        "cycle " + e + " is a permutation");
                cycles.add(codes);
            }
            // Same space every cycle, refilled in a different order.
            assertEquals(new HashSet<>(cycles.get(0)), new HashSet<>(cycles.get(1)));
            assertEquals(new HashSet<>(cycles.get(1)), new HashSet<>(cycles.get(2)));
            assertNotEquals(cycles.get(0), cycles.get(1));
            assertNotEquals(cycles.get(1), cycles.get(2));
            assertNotEquals(cycles.get(0), cycles.get(2));
        }

        @Test
        void wrongCycleDecodesToADifferentCounterNotAnError() {
            CyclingDealcode codec = CyclingDealcode.builder()
                    .key("k").alphabet("crockford").length(6).build();
            String code = codec.encode(7);
            assertEquals(7, codec.decode(code, 0));
            // Documented ambiguity: the cycle is context the application
            // must supply; a wrong (but in-range) cycle simply yields a
            // different counter.
            assertNotEquals(7, codec.decode(code, 1));
        }

        @Test
        void topCounterRoundtripsInTheFinalPartialCycle() {
            CyclingDealcode codec = CyclingDealcode.builder()
                    .key("k").alphabet("dec").length(2).build();
            long top = Long.MAX_VALUE; // 2^63 - 1
            long cycle = codec.cycleOf(top);
            assertEquals(codec.maxCycle(), cycle);
            String code = codec.encode(top);
            assertEquals(top, codec.decode(code, cycle));
            // 2^63 - 1 = maxCycle * 100 + 7, so the final cycle issues only
            // v = 0..7 (8 codes); the other 92 two-digit codes decode there
            // to counters >= 2^63 and must be rejected.
            Set<String> issued = new HashSet<>();
            for (long v = 0; v <= 7; v++) {
                issued.add(codec.encode(cycle * 100 + v));
            }
            int rejected = 0;
            for (int a = 0; a < 10; a++) {
                for (int b = 0; b < 10; b++) {
                    String candidate = "" + a + b;
                    if (issued.contains(candidate)) {
                        assertEquals(cycle, codec.cycleOf(codec.decode(candidate, cycle)));
                    } else {
                        assertThrows(InvalidCodeException.class,
                                () -> codec.decode(candidate, cycle),
                                "never-issued code " + candidate);
                        rejected++;
                    }
                }
            }
            assertEquals(92, rejected);
        }
    }

    @Nested
    class FullCapacitySingleCycle {

        /** radix 8, length 21: capacity 8^21 == 2^63 exactly — one full cycle. */
        private CyclingDealcode octal21() {
            return CyclingDealcode.builder()
                    .key(KEY_16).alphabet("01234567").length(21).build();
        }

        @Test
        void capacityIsExactlyTwoPow63AndMaxCycleIsZero() {
            CyclingDealcode codec = octal21();
            assertEquals(BigInteger.ONE.shiftLeft(63), codec.capacity());
            assertEquals(0L, codec.maxCycle());
            assertEquals(21, codec.length());
            assertEquals(8, codec.radix());
        }

        @Test
        void encodesDecodeAtCycleZero() {
            CyclingDealcode codec = octal21();
            String code = codec.encode(5);
            assertEquals(21, code.length());
            assertEquals(0L, codec.cycleOf(5));
            assertEquals(5, codec.decode(code, 0));
        }

        @Test
        void cycleOneIsOutOfRange() {
            CyclingDealcode codec = octal21();
            String code = codec.encode(5);
            assertThrows(CounterRangeException.class, () -> codec.decode(code, 1));
        }

        @Test
        void topCounterRoundtrips() {
            CyclingDealcode codec = octal21();
            long top = Long.MAX_VALUE;
            assertEquals(0L, codec.cycleOf(top));
            assertEquals(top, codec.decode(codec.encode(top), 0));
        }

        @Test
        void oneLengthMoreOverflowsTheCounterSpaceAndIsRejected() {
            assertThrows(ConfigException.class, () -> CyclingDealcode.builder()
                    .key(KEY_16).alphabet("01234567").length(22).build());
        }
    }

    @Nested
    class Guards {

        @Test
        void encodeRejectsNegativeCounters() {
            CyclingDealcode codec = CyclingDealcode.builder().key("k").build();
            assertThrows(CounterRangeException.class, () -> codec.encode(-1));
            assertThrows(CounterRangeException.class, () -> codec.encode(Long.MIN_VALUE));
        }

        @Test
        void cycleOfRejectsNegativeCounters() {
            CyclingDealcode codec = CyclingDealcode.builder().key("k").build();
            assertThrows(CounterRangeException.class, () -> codec.cycleOf(-1));
        }

        @Test
        void decodeRejectsBadCyclesBeforeLookingAtTheCode() {
            CyclingDealcode codec = CyclingDealcode.builder().key("k").build();
            String code = codec.encode(0);
            assertThrows(CounterRangeException.class, () -> codec.decode(code, -1));
            assertThrows(CounterRangeException.class,
                    () -> codec.decode(code, codec.maxCycle() + 1));
            // The cycle gate fires even for garbage codes.
            assertThrows(CounterRangeException.class, () -> codec.decode("", -1));
        }

        @Test
        void decodeRejectsMalformedCodes() {
            CyclingDealcode codec = CyclingDealcode.builder().key("k").build(); // hex, length 6
            assertThrows(InvalidCodeException.class, () -> codec.decode(null, 0));
            assertThrows(InvalidCodeException.class, () -> codec.decode("", 0));
            assertThrows(InvalidCodeException.class, () -> codec.decode("00000", 0));
            assertThrows(InvalidCodeException.class, () -> codec.decode("0000000", 0));
            assertThrows(InvalidCodeException.class, () -> codec.decode("00000g", 0));
            // Oversized garbage is rejected by the O(1) length gate.
            assertThrows(InvalidCodeException.class,
                    () -> codec.decode("0".repeat(100_000), 0));
        }

        @Test
        void builderRejectsBadLengthsBeforeAndAfterThePowerCheck() {
            // Bounds first — absurd lengths fail before any power is computed.
            for (int bad : new int[] {Integer.MIN_VALUE, -1, 0, 1, 129, Integer.MAX_VALUE}) {
                int length = bad;
                assertThrows(ConfigException.class, () -> CyclingDealcode.builder()
                        .key("k").alphabet("dec").length(length).build());
            }
            // radix^length < 100 (FF1 structural minimum).
            assertThrows(ConfigException.class, () -> CyclingDealcode.builder()
                    .key("k").alphabet("abcdefghi").length(2).build()); // 9^2 = 81
            // radix^length > 2^63.
            assertThrows(ConfigException.class, () -> CyclingDealcode.builder()
                    .key("k").alphabet("hex").length(16).build()); // 16^16 = 2^64
        }

        @Test
        void builderAppliesTheSharedKeyAndAlphabetGuards() {
            assertThrows(ConfigException.class,
                    () -> CyclingDealcode.builder().build()); // key required
            assertThrows(ConfigException.class,
                    () -> CyclingDealcode.builder().key("").build());
            assertThrows(ConfigException.class, () -> CyclingDealcode.builder()
                    .key("crockford").build()); // preset name as key: swapped args
            assertThrows(ConfigException.class, () -> CyclingDealcode.builder()
                    .key("k").alphabet("HEX").build()); // miscased preset
            assertThrows(ConfigException.class, () -> CyclingDealcode.builder()
                    .key("k").domain("x".repeat(256)).build());
        }

        @Test
        void sameKeyAndConfigMatchesAcrossModesOnlyInBeingUnrelated() {
            // Cycling tweaks live in "dealcode/v1c/", disjoint from plain v1:
            // a fixed-length plain codec with the same key/alphabet/length
            // must not produce the same permutation as cycle 0.
            Dealcode plain = Dealcode.builder()
                    .key(KEY_16).alphabet("hex").minLength(6).maxLength(6).build();
            CyclingDealcode cycling = CyclingDealcode.builder()
                    .key(KEY_16).alphabet("hex").length(6).build();
            boolean anyDiffer = false;
            for (long n = 0; n < 20; n++) {
                anyDiffer |= !plain.encode(n).equals(cycling.encode(n));
            }
            assertEquals(true, anyDiffer, "v1 and v1c permutations must be unrelated");
        }

        @Test
        void toStringNeverContainsTheKey() {
            String secret = "super-secret-key-material";
            CyclingDealcode codec = CyclingDealcode.builder()
                    .key(secret).alphabet("crockford").length(6).domain("bookings").build();
            String s = codec.toString();
            assertFalse(s.contains(secret));
            assertEquals("CyclingDealcode(alphabet=crockford, length=6, domain=\"bookings\")", s);
        }
    }
}
