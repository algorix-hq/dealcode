package io.algorix.dealcode;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.math.BigInteger;
import java.util.ArrayList;
import java.util.List;
import org.junit.jupiter.api.Nested;
import org.junit.jupiter.api.Test;

/** Behavioural tests for integer range mode beyond the shared spec vectors. */
class RangeBehaviourTest {

    @Nested
    class Bijection {

        @Test
        void smallRangeWithNoDeadZoneIsAFullBijection() {
            // N = 121 = 11^2, an admissible power: capacity == N exactly.
            RangeDealcode codec = RangeDealcode.builder()
                    .key("k").low(1_000).high(1_120).build();
            assertEquals(BigInteger.valueOf(121), codec.capacity());

            List<Long> codes = new ArrayList<>();
            for (long n = 0; n < 121; n++) {
                long code = codec.encode(n);
                assertTrue(code >= 1_000 && code <= 1_120, "code " + code + " in range");
                assertEquals(n, codec.decode(code), "roundtrip of " + n);
                codes.add(code);
            }
            // Every value of the range issued exactly once.
            List<Long> sorted = new ArrayList<>(codes);
            sorted.sort(null);
            for (long c = 1_000; c <= 1_120; c++) {
                assertEquals(c, sorted.get((int) (c - 1_000)));
            }
        }
    }

    @Nested
    class DeadZone {

        @Test
        void deadZoneRejectedButIssuedTopAccepted() {
            RangeDealcode codec = RangeDealcode.builder()
                    .key("k").low(100_000).high(999_999).build();
            long capacity = codec.capacity().longValueExact(); // 96^3 = 884736
            assertEquals(884_736, capacity);
            long topIssued = codec.low() + capacity - 1; // 984735

            long topCode = codec.encode(capacity - 1);
            assertTrue(topCode >= 100_000 && topCode <= topIssued);
            assertEquals(capacity - 1, codec.decode(topCode));

            for (long dead : new long[] {topIssued + 1, 999_999}) {
                assertThrows(InvalidCodeException.class, () -> codec.decode(dead),
                        "dead-zone code " + dead);
            }
            assertThrows(InvalidCodeException.class, () -> codec.decode(99_999));
            assertThrows(InvalidCodeException.class, () -> codec.decode(1_000_000));
        }

        @Test
        void counterAtCapacityRejected() {
            RangeDealcode codec = RangeDealcode.builder()
                    .key("k").low(100_000).high(999_999).build();
            long capacity = codec.capacity().longValueExact();
            assertThrows(CounterRangeException.class, () -> codec.encode(capacity));
            assertThrows(CounterRangeException.class, () -> codec.encode(-1));
        }
    }

    @Nested
    class TweakBinding {

        @Test
        void lowHighAndDomainEachBindThePermutation() {
            RangeDealcode a = RangeDealcode.builder()
                    .key("k").low(100_000).high(999_999).build();
            RangeDealcode b = RangeDealcode.builder()
                    .key("k").low(100_000).high(999_998).build();
            RangeDealcode c = RangeDealcode.builder()
                    .key("k").low(100_000).high(999_999).domain("x").build();
            // b differs from a only in high (same derived domain: 96^3 fits
            // both spans); c only in domain — yet all three disagree.
            assertEquals(a.capacity(), b.capacity());
            List<List<Long>> outs = new ArrayList<>();
            for (RangeDealcode codec : new RangeDealcode[] {a, b, c}) {
                List<Long> codes = new ArrayList<>();
                for (long n = 0; n < 8; n++) {
                    codes.add(codec.encode(n));
                }
                outs.add(codes);
            }
            assertNotEquals(outs.get(0), outs.get(1));
            assertNotEquals(outs.get(0), outs.get(2));
            assertNotEquals(outs.get(1), outs.get(2));
        }

        @Test
        void lowShiftsMoreThanTheOffset() {
            // Same span, different low: not merely the same permutation
            // shifted by the offset — low is bound into the tweak.
            RangeDealcode a = RangeDealcode.builder()
                    .key("k").low(0).high(899_999).build();
            RangeDealcode b = RangeDealcode.builder()
                    .key("k").low(100_000).high(999_999).build();
            assertEquals(a.capacity(), b.capacity());
            boolean allShifted = true;
            for (long n = 0; n < 8; n++) {
                allShifted &= (a.encode(n) + 100_000 == b.encode(n));
            }
            assertFalse(allShifted);
        }
    }

    @Nested
    class Introspection {

        @Test
        void accessorsAndToStringHideTheKey() {
            RangeDealcode codec = RangeDealcode.builder()
                    .key("super-secret").low(100_000).high(999_999).domain("d").build();
            assertEquals(100_000, codec.low());
            assertEquals(999_999, codec.high());
            assertEquals("d", codec.domain());
            assertEquals(96, codec.radix());
            assertFalse(codec.toString().contains("secret"));
            assertEquals("RangeDealcode(low=100000, high=999999, domain=\"d\")",
                    codec.toString());
        }
    }

    @Nested
    class Config {

        @Test
        void missingBoundsRejected() {
            assertThrows(ConfigException.class,
                    () -> RangeDealcode.builder().key("k").build());
            assertThrows(ConfigException.class,
                    () -> RangeDealcode.builder().key("k").low(0).build());
            assertThrows(ConfigException.class,
                    () -> RangeDealcode.builder().key("k").high(999).build());
        }

        @Test
        void missingKeyRejected() {
            assertThrows(ConfigException.class,
                    () -> RangeDealcode.builder().low(0).high(999).build());
        }

        @Test
        void spanOf99Rejected100Accepted() {
            assertThrows(ConfigException.class,
                    () -> RangeDealcode.builder().key("k").low(0).high(98).build());
            RangeDealcode codec = RangeDealcode.builder()
                    .key("k").low(0).high(99).build();
            assertEquals(BigInteger.valueOf(100), codec.capacity());
            assertEquals(10, codec.radix());
        }

        @Test
        void fullCounterSpaceIsAdmissible() {
            // [0, 2^63 - 1]: N = 2^63 = 128^9 exactly — no dead zone, and a
            // capacity only BigInteger can represent.
            RangeDealcode codec = RangeDealcode.builder()
                    .key("k").low(0).high(Long.MAX_VALUE).build();
            assertEquals(BigInteger.ONE.shiftLeft(63), codec.capacity());
            assertEquals(128, codec.radix());
            assertEquals(Long.MAX_VALUE,
                    codec.decode(codec.encode(Long.MAX_VALUE)));
        }
    }
}
