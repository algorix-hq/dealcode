package io.algorix.dealcode;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;

import com.fasterxml.jackson.databind.JsonNode;
import java.util.ArrayList;
import java.util.HexFormat;
import java.util.List;
import org.junit.jupiter.api.DynamicTest;
import org.junit.jupiter.api.TestFactory;

/**
 * The 9 official NIST FF1-AES sample vectors (testvectors/ff1_nist.json),
 * exercised in both directions. Plaintext/ciphertext strings use the alphabet
 * 0123456789abcdefghijklmnopqrstuvwxyz (character index = numeral value).
 */
class Ff1NistVectorsTest {

    private static final String ALPHABET = "0123456789abcdefghijklmnopqrstuvwxyz";

    @TestFactory
    List<DynamicTest> nistSamples() {
        JsonNode root = TestVectors.load("ff1_nist.json");
        List<DynamicTest> tests = new ArrayList<>();
        for (JsonNode v : root.get("vectors")) {
            int sample = v.get("sample").asInt();
            String cipherName = v.get("cipher").asText();
            byte[] key = HexFormat.of().parseHex(v.get("key_hex").asText());
            byte[] tweak = HexFormat.of().parseHex(v.get("tweak_hex").asText());
            int radix = v.get("radix").asInt();
            int[] plaintext = toNumerals(v.get("plaintext").asText());
            int[] ciphertext = toNumerals(v.get("ciphertext").asText());

            tests.add(DynamicTest.dynamicTest(
                    "sample " + sample + " " + cipherName + " encrypt", () -> {
                        FF1 ff1 = new FF1(key, radix);
                        FF1.Params p = ff1.params(tweak, plaintext.length);
                        assertArrayEquals(ciphertext, ff1.encrypt(p, plaintext));
                    }));
            tests.add(DynamicTest.dynamicTest(
                    "sample " + sample + " " + cipherName + " decrypt", () -> {
                        FF1 ff1 = new FF1(key, radix);
                        FF1.Params p = ff1.params(tweak, ciphertext.length);
                        assertArrayEquals(plaintext, ff1.decrypt(p, ciphertext));
                    }));
        }
        return tests;
    }

    private static int[] toNumerals(String s) {
        int[] out = new int[s.length()];
        for (int i = 0; i < s.length(); i++) {
            int idx = ALPHABET.indexOf(s.charAt(i));
            if (idx < 0) {
                throw new IllegalArgumentException("bad vector char: " + s.charAt(i));
            }
            out[i] = idx;
        }
        return out;
    }
}
