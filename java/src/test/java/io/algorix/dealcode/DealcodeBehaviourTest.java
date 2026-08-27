package io.algorix.dealcode;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;
import java.util.concurrent.Callable;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import org.junit.jupiter.api.Nested;
import org.junit.jupiter.api.Test;

/** Behavioural tests beyond the shared spec vectors. */
class DealcodeBehaviourTest {

    private static final byte[] KEY_16 = new byte[] {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
    };

    @Nested
    class Roundtrip {

        @Test
        void thousandsOfCountersIncludingStageBoundaries() {
            Dealcode codec = Dealcode.builder()
                    .key(KEY_16).alphabet("hex").minLength(4).maxLength(6).build();
            long r4 = 65536L;       // 16^4
            long r5 = 1_048_576L;   // 16^5
            long r6 = 16_777_216L;  // 16^6

            Set<Long> counters = new LinkedHashSet<>();
            for (long n = 0; n < 3000; n++) {
                counters.add(n);
            }
            for (long b : new long[] {r4, r5, r6}) {
                for (long n = b - 50; n < b + 50 && n < r6; n++) {
                    counters.add(n);
                }
            }
            counters.add(r6 - 1);

            Set<String> codes = new HashSet<>();
            for (long n : counters) {
                String code = codec.encode(n);
                int expectedLen = n < r4 ? 4 : n < r5 ? 5 : 6;
                assertEquals(expectedLen, code.length(), "length of encode(" + n + ")");
                assertEquals(n, codec.decode(code), "roundtrip of " + n);
                assertTrue(codes.add(code), "collision at counter " + n + ": " + code);
            }
        }

        @Test
        void firstAndLastCountersOfEveryStage() {
            Dealcode codec = Dealcode.builder()
                    .key("stage-edges").alphabet("dec").minLength(4).maxLength(9).build();
            assertEquals(999_999_999L, codec.maxCounter());
            long pow = 1;
            for (int d = 0; d < 4; d++) {
                pow *= 10;
            }
            // stage minLength: [0, 10^4); stage d: [10^(d-1), 10^d)
            long[] edges = {0, 1, pow - 1, pow};
            for (long e : edges) {
                assertEquals(e, codec.decode(codec.encode(e)));
            }
            for (int d = 5; d <= 9; d++) {
                pow *= 10;
                assertEquals(pow - 1, codec.decode(codec.encode(pow - 1)));
                assertEquals(d, codec.encode(pow - 1).length());
                if (pow - 1 < codec.maxCounter()) {
                    assertEquals(d + 1, codec.encode(pow).length());
                }
            }
            assertThrows(CounterRangeException.class,
                    () -> codec.encode(codec.maxCounter() + 1));
            assertThrows(CounterRangeException.class, () -> codec.encode(-1));
        }

        @Test
        void defaultsMatchSpec() {
            Dealcode hex = Dealcode.builder().key("k").build();
            assertEquals(Alphabets.HEX, hex.alphabet());
            assertEquals(16, hex.radix());
            assertEquals(6, hex.minLength());
            assertEquals(15, hex.maxLength()); // largest L with 16^L <= 2^63-1
            assertEquals("", hex.domain());

            assertEquals(18, Dealcode.builder().key("k").alphabet("dec").build().maxLength());
            assertEquals(12, Dealcode.builder().key("k").alphabet("base32").build().maxLength());
            assertEquals(12, Dealcode.builder().key("k").alphabet("crockford").build().maxLength());
            assertEquals(12, Dealcode.builder().key("k").alphabet("base36").build().maxLength());
            assertEquals(10, Dealcode.builder().key("k").alphabet("base58").build().maxLength());
            assertEquals(10, Dealcode.builder().key("k").alphabet("base62").build().maxLength());
            assertEquals(10, Dealcode.builder().key("k").alphabet("base64url").build().maxLength());
        }

        @Test
        void domainsProduceUnrelatedCodes() {
            Dealcode a = Dealcode.builder().key(KEY_16).domain("orders").build();
            Dealcode b = Dealcode.builder().key(KEY_16).domain("coupons").build();
            assertNotEquals(a.encode(42), b.encode(42));
        }
    }

    @Nested
    class ConfigErrors {

        @Test
        void missingOrEmptyKey() {
            assertThrows(ConfigException.class, () -> Dealcode.builder().build());
            assertThrows(ConfigException.class,
                    () -> Dealcode.builder().key(new byte[0]).build());
            assertThrows(ConfigException.class, () -> Dealcode.builder().key("").build());
            assertThrows(ConfigException.class, () -> Dealcode.builder().key((byte[]) null));
            assertThrows(ConfigException.class, () -> Dealcode.builder().key((String) null));
        }

        @Test
        void badAlphabets() {
            // too short / too long
            assertThrows(ConfigException.class,
                    () -> Dealcode.builder().key("k").alphabet("a").build());
            StringBuilder tooLong = new StringBuilder();
            for (char c = 0x21; tooLong.length() < 95; c++) {
                tooLong.append(c);
            }
            assertThrows(ConfigException.class,
                    () -> Dealcode.builder().key("k").alphabet(tooLong.toString()).build());
            // duplicate characters
            assertThrows(ConfigException.class,
                    () -> Dealcode.builder().key("k").alphabet("abca").build());
            // space / control / non-ASCII
            assertThrows(ConfigException.class,
                    () -> Dealcode.builder().key("k").alphabet("ab cd").build());
            assertThrows(ConfigException.class,
                    () -> Dealcode.builder().key("k").alphabet("ab\tcd").build());
            assertThrows(ConfigException.class,
                    () -> Dealcode.builder().key("k").alphabet("abcé").build());
            assertThrows(ConfigException.class, () -> Dealcode.builder().alphabet(null));
        }

        @Test
        void badLengths() {
            assertThrows(ConfigException.class,
                    () -> Dealcode.builder().key("k").minLength(1).build());
            // radix^minLength < 100: 10^1, and binary alphabet 2^6 = 64
            assertThrows(ConfigException.class,
                    () -> Dealcode.builder().key("k").alphabet("01").minLength(6).build());
            // maxLength < minLength
            assertThrows(ConfigException.class,
                    () -> Dealcode.builder().key("k").minLength(8).maxLength(7).build());
            // radix^maxLength > 2^128: 16^33 = 2^132
            assertThrows(ConfigException.class,
                    () -> Dealcode.builder().key("k").maxLength(33).build());
            // 16^32 = 2^128 is allowed (boundary)
            assertEquals(32, Dealcode.builder().key("k").maxLength(32).build().maxLength());
        }

        @Test
        void badDomain() {
            assertThrows(ConfigException.class, () -> Dealcode.builder().domain(null));
            String longDomain = "é".repeat(128); // 256 UTF-8 bytes
            assertThrows(ConfigException.class,
                    () -> Dealcode.builder().key("k").domain(longDomain).build());
            // exactly 255 bytes is fine
            Dealcode.builder().key("k").domain("x".repeat(255)).build();
        }

        @Test
        void exceptionsAreIllegalArgumentExceptions() {
            assertTrue(DealcodeException.class
                    .isAssignableFrom(ConfigException.class));
            assertTrue(DealcodeException.class
                    .isAssignableFrom(CounterRangeException.class));
            assertTrue(DealcodeException.class
                    .isAssignableFrom(InvalidCodeException.class));
            assertTrue(IllegalArgumentException.class
                    .isAssignableFrom(DealcodeException.class));
        }
    }

    @Nested
    class KeyMaterial {

        @Test
        void directAesKeysOfEachSizeWork() {
            for (int size : new int[] {16, 24, 32}) {
                byte[] key = new byte[size];
                key[0] = 7;
                Dealcode codec = Dealcode.builder().key(key).build();
                assertEquals(123L, codec.decode(codec.encode(123L)));
            }
        }

        @Test
        void oddSizedBytesAndStringsAreDerivedConsistently() throws Exception {
            // Non-16/24/32 byte keys are derived from their raw bytes, and a
            // string key is derived from its UTF-8 bytes with the same rule —
            // so a 20-byte passphrase gives identical codes both ways.
            String passphrase = "hunter2-hunter2-1234"; // 20 ASCII bytes
            Dealcode fromString = Dealcode.builder().key(passphrase).build();
            Dealcode fromBytes = Dealcode.builder()
                    .key(passphrase.getBytes(StandardCharsets.UTF_8)).build();
            for (long n : new long[] {0, 1, 42, 999_999}) {
                assertEquals(fromString.encode(n), fromBytes.encode(n));
            }

            // The derivation is SHA-256("dealcode/v1/kdf" || material) used
            // directly as an AES-256 key.
            MessageDigest sha = MessageDigest.getInstance("SHA-256");
            sha.update("dealcode/v1/kdf".getBytes(StandardCharsets.US_ASCII));
            byte[] derived = sha.digest(passphrase.getBytes(StandardCharsets.UTF_8));
            Dealcode fromDerived = Dealcode.builder().key(derived).build();
            assertEquals(fromDerived.encode(42), fromString.encode(42));
        }

        @Test
        void stringKeysAreNeverAutoDecoded() {
            // A 32-character string is derived; the same 32 raw bytes are a
            // direct AES-256 key — they must produce different codes.
            String s = "0123456789abcdef0123456789abcdef";
            Dealcode fromString = Dealcode.builder().key(s).build();
            Dealcode fromBytes = Dealcode.builder()
                    .key(s.getBytes(StandardCharsets.US_ASCII)).build();
            assertNotEquals(fromString.encode(42), fromBytes.encode(42));
        }

        @Test
        void keyArrayIsCopied() {
            byte[] key = KEY_16.clone();
            Dealcode codec = Dealcode.builder().key(key).build();
            String before = codec.encode(42);
            key[0] ^= (byte) 0xFF; // caller mutates the array afterwards
            assertEquals(before, codec.encode(42));
        }
    }

    @Nested
    class Concurrency {

        @Test
        void sharedInstanceIsThreadSafe() throws Exception {
            Dealcode codec = Dealcode.builder()
                    .key(KEY_16).alphabet("base62").minLength(4).maxLength(12).build();
            // Expected values computed single-threaded first.
            int perThread = 2000;
            int threads = 8;
            String[] expected = new String[perThread * threads];
            for (int i = 0; i < expected.length; i++) {
                expected[i] = codec.encode(i);
            }

            ExecutorService pool = Executors.newFixedThreadPool(threads);
            try {
                List<Callable<Void>> jobs = new ArrayList<>();
                for (int t = 0; t < threads; t++) {
                    final int offset = t * perThread;
                    jobs.add(() -> {
                        for (int i = 0; i < perThread; i++) {
                            long n = offset + i;
                            String code = codec.encode(n);
                            assertEquals(expected[(int) n], code,
                                    "concurrent encode(" + n + ")");
                            assertEquals(n, codec.decode(code),
                                    "concurrent decode of counter " + n);
                        }
                        return null;
                    });
                }
                for (Future<Void> f : pool.invokeAll(jobs)) {
                    f.get(); // propagate assertion failures
                }
            } finally {
                pool.shutdownNow();
            }
        }
    }

    @Nested
    class LargeCodeSpace {

        @Test
        void fixedLengthHex16CoversFullLongRange() {
            // radix^maxLength = 16^16 = 2^64 > 2^63: the counter space is
            // capped at 2^63 and maxCounter is exactly Long.MAX_VALUE.
            Dealcode codec = Dealcode.builder()
                    .key(KEY_16).minLength(16).maxLength(16).build();
            assertEquals(Long.MAX_VALUE, codec.maxCounter());

            String code = codec.encode(Long.MAX_VALUE);
            assertEquals(16, code.length());
            assertEquals(Long.MAX_VALUE, codec.decode(code));
            assertEquals(0L, codec.decode(codec.encode(0L)));
            assertThrows(CounterRangeException.class, () -> codec.encode(-1L));
        }

        @Test
        void decodeRejectsCodesOutsideTheCounterSpace() {
            // Half of all 16-char hex strings decrypt to values >= 2^63 and
            // were therefore never issued; decode must reject them. Scan a
            // deterministic sample: each candidate is independently ~50% to
            // be invalid, so 64 candidates fail to find one with p ~ 2^-64.
            Dealcode codec = Dealcode.builder()
                    .key(KEY_16).minLength(16).maxLength(16).build();
            int rejected = 0;
            int accepted = 0;
            for (int i = 0; i < 64; i++) {
                String candidate = String.format("%016x", 0xdeadbeefL + i);
                try {
                    long n = codec.decode(candidate);
                    assertTrue(n >= 0);
                    assertEquals(candidate, codec.encode(n)); // consistent both ways
                    accepted++;
                } catch (InvalidCodeException e) {
                    rejected++;
                }
            }
            assertTrue(rejected > 0, "expected some codes outside the 2^63 counter space");
            assertTrue(accepted > 0, "expected some codes inside the counter space");
        }
    }

    @Nested
    class Decoding {

        @Test
        void normalizationRules() {
            Dealcode hex = Dealcode.builder().key(KEY_16).build();
            long n = hex.decode(hex.encode(123456).toUpperCase(java.util.Locale.ROOT));
            assertEquals(123456L, n);

            Dealcode crock = Dealcode.builder().key(KEY_16).alphabet("crockford").build();
            String code = crock.encode(77);
            String confused = code.toLowerCase(java.util.Locale.ROOT)
                    .replace('0', 'O').replace('1', 'l');
            assertEquals(77L, crock.decode(confused));

            // Custom alphabets: exact match only.
            Dealcode custom = Dealcode.builder()
                    .key(KEY_16).alphabet("abcdefghij").minLength(4).build();
            String c = custom.encode(5);
            assertThrows(InvalidCodeException.class,
                    () -> custom.decode(c.toUpperCase(java.util.Locale.ROOT)));
            assertEquals(5L, custom.decode(c));
        }

        @Test
        void malformedInput() {
            Dealcode codec = Dealcode.builder().key(KEY_16).build(); // hex 6..15
            assertThrows(InvalidCodeException.class, () -> codec.decode(null));
            assertThrows(InvalidCodeException.class, () -> codec.decode(""));
            assertThrows(InvalidCodeException.class, () -> codec.decode("00000"));
            assertThrows(InvalidCodeException.class, () -> codec.decode("0".repeat(16)));
            assertThrows(InvalidCodeException.class, () -> codec.decode("00g000"));
            assertThrows(InvalidCodeException.class, () -> codec.decode("00000é"));
        }

        @Test
        void toStringNeverContainsKeyMaterial() {
            Dealcode codec = Dealcode.builder()
                    .key("super-secret-key").domain("orders").build();
            String repr = codec.toString();
            assertTrue(repr.contains("hex"));
            assertTrue(repr.contains("orders"));
            assertTrue(!repr.contains("super-secret-key"));
        }
    }
}
