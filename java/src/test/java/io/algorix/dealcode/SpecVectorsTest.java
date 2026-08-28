package io.algorix.dealcode;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

import com.fasterxml.jackson.databind.JsonNode;
import java.math.BigInteger;
import java.util.ArrayList;
import java.util.HexFormat;
import java.util.List;
import org.junit.jupiter.api.DynamicContainer;
import org.junit.jupiter.api.DynamicNode;
import org.junit.jupiter.api.DynamicTest;
import org.junit.jupiter.api.TestFactory;

/**
 * Conformance to the dealcode format-v1 vectors (testvectors/v1.json): every
 * vector encodes and decodes exactly, every invalid code is rejected with
 * {@link InvalidCodeException}, every normalization case decodes to its
 * counter, every out-of-range counter is rejected with
 * {@link CounterRangeException}, and every invalid config is rejected with
 * {@link ConfigException} at build time.
 */
class SpecVectorsTest {

    @TestFactory
    List<DynamicNode> v1Configs() {
        JsonNode root = TestVectors.load("v1.json");
        List<DynamicNode> containers = new ArrayList<>();
        for (JsonNode config : root.get("configs")) {
            containers.add(configContainer(config));
        }
        return containers;
    }

    private static DynamicContainer configContainer(JsonNode config) {
        String name = config.get("name").asText();
        Dealcode codec = buildCodec(config);
        List<DynamicNode> tests = new ArrayList<>();

        for (JsonNode vector : config.get("vectors")) {
            long n = Long.parseLong(vector.get("n").asText());
            String code = vector.get("code").asText();
            tests.add(DynamicTest.dynamicTest("encode(" + n + ") == \"" + code + "\"",
                    () -> assertEquals(code, codec.encode(n))));
            tests.add(DynamicTest.dynamicTest("decode(\"" + code + "\") == " + n,
                    () -> assertEquals(n, codec.decode(code))));
        }

        for (JsonNode invalid : config.get("invalid_codes")) {
            String code = invalid.asText();
            tests.add(DynamicTest.dynamicTest("decode(\"" + code + "\") rejected",
                    () -> assertThrows(InvalidCodeException.class, () -> codec.decode(code))));
        }

        for (JsonNode norm : config.get("normalize")) {
            String input = norm.get("input").asText();
            long n = Long.parseLong(norm.get("n").asText());
            tests.add(DynamicTest.dynamicTest("decode(\"" + input + "\") normalizes to " + n,
                    () -> assertEquals(n, codec.decode(input))));
        }

        for (JsonNode counter : config.get("range_counters")) {
            String raw = counter.asText();
            BigInteger big = new BigInteger(raw);
            if (big.bitLength() > 63) {
                // 2^63 and 2^64 don't fit a signed long, so they can't even be
                // passed to encode(long) — Java's type system covers them.
                continue;
            }
            long n = big.longValueExact();
            tests.add(DynamicTest.dynamicTest("encode(" + raw + ") rejected (out of range)",
                    () -> assertThrows(CounterRangeException.class, () -> codec.encode(n))));
        }

        return DynamicContainer.dynamicContainer(name, tests);
    }

    @TestFactory
    List<DynamicNode> v1InvalidConfigs() {
        JsonNode root = TestVectors.load("v1.json");
        List<DynamicNode> tests = new ArrayList<>();
        for (JsonNode config : root.get("invalid_configs")) {
            String name = config.get("name").asText();
            tests.add(DynamicTest.dynamicTest(name + " rejected at build time",
                    () -> assertThrows(ConfigException.class, () -> buildInvalidCodec(config))));
        }
        return tests;
    }

    /**
     * Builds a codec from an {@code invalid_configs} entry, which unlike a
     * valid config has optional fields and names a custom alphabet directly
     * via {@code custom_alphabet}.
     */
    private static Dealcode buildInvalidCodec(JsonNode config) {
        Dealcode.Builder builder = Dealcode.builder();
        if (config.has("key_hex")) {
            builder.key(HexFormat.of().parseHex(config.get("key_hex").asText()));
        } else {
            builder.key(config.get("key_string").asText());
        }
        if (config.has("custom_alphabet")) {
            builder.alphabet(config.get("custom_alphabet").asText());
        } else if (config.has("alphabet")) {
            builder.alphabet(config.get("alphabet").asText());
        }
        if (config.has("min_length")) {
            builder.minLength(config.get("min_length").asInt());
        }
        if (config.has("max_length")) {
            builder.maxLength(config.get("max_length").asInt());
        }
        if (config.has("domain")) {
            builder.domain(config.get("domain").asText());
        }
        return builder.build();
    }

    private static Dealcode buildCodec(JsonNode config) {
        Dealcode.Builder builder = Dealcode.builder();
        if (config.has("key_hex")) {
            builder.key(HexFormat.of().parseHex(config.get("key_hex").asText()));
        } else {
            builder.key(config.get("key_string").asText());
        }
        String alphabet = config.get("alphabet").asText();
        if ("custom".equals(alphabet)) {
            builder.alphabet(config.get("custom_alphabet").asText());
        } else {
            builder.alphabet(alphabet);
        }
        return builder
                .minLength(config.get("min_length").asInt())
                .maxLength(config.get("max_length").asInt())
                .domain(config.get("domain").asText())
                .build();
    }
}
