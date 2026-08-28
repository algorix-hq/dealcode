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
 * Conformance to the fixed-length cycling-mode vectors (testvectors/v1c.json,
 * SPEC §11.4): every config's capacity and max cycle match, every vector
 * encodes and decodes exactly under its counter's cycle, every invalid code
 * is rejected with {@link InvalidCodeException} for the cycle it names, every
 * normalization case decodes to its counter, every out-of-range counter and
 * cycle is rejected with {@link CounterRangeException}, and every invalid
 * config is rejected with {@link ConfigException} at build time.
 */
class CyclingSpecVectorsTest {

    @TestFactory
    List<DynamicNode> v1cConfigs() {
        JsonNode root = TestVectors.load("v1c.json");
        List<DynamicNode> containers = new ArrayList<>();
        for (JsonNode config : root.get("configs")) {
            containers.add(configContainer(config));
        }
        return containers;
    }

    private static DynamicContainer configContainer(JsonNode config) {
        String name = config.get("name").asText();
        CyclingDealcode codec = buildCodec(config);
        List<DynamicNode> tests = new ArrayList<>();

        // capacity/max_cycle are decimal strings; capacity may be exactly
        // 2^63, which only BigInteger represents — capacity() is BigInteger
        // for precisely that reason.
        BigInteger capacity = new BigInteger(config.get("capacity").asText());
        long maxCycle = Long.parseLong(config.get("max_cycle").asText());
        tests.add(DynamicTest.dynamicTest("capacity() == " + capacity,
                () -> assertEquals(capacity, codec.capacity())));
        tests.add(DynamicTest.dynamicTest("maxCycle() == " + maxCycle,
                () -> assertEquals(maxCycle, codec.maxCycle())));

        for (JsonNode vector : config.get("vectors")) {
            long n = Long.parseLong(vector.get("n").asText());
            String code = vector.get("code").asText();
            tests.add(DynamicTest.dynamicTest("encode(" + n + ") == \"" + code + "\"",
                    () -> assertEquals(code, codec.encode(n))));
            tests.add(DynamicTest.dynamicTest(
                    "decode(\"" + code + "\", cycleOf(" + n + ")) == " + n,
                    () -> assertEquals(n, codec.decode(code, codec.cycleOf(n)))));
        }

        for (JsonNode invalid : config.get("invalid_codes")) {
            String code = invalid.get("code").asText();
            long cycle = Long.parseLong(invalid.get("cycle").asText());
            tests.add(DynamicTest.dynamicTest(
                    "decode(\"" + code + "\", " + cycle + ") rejected",
                    () -> assertThrows(InvalidCodeException.class,
                            () -> codec.decode(code, cycle))));
        }

        for (JsonNode norm : config.get("normalize")) {
            String input = norm.get("input").asText();
            long cycle = Long.parseLong(norm.get("cycle").asText());
            long n = Long.parseLong(norm.get("n").asText());
            tests.add(DynamicTest.dynamicTest(
                    "decode(\"" + input + "\", " + cycle + ") normalizes to " + n,
                    () -> assertEquals(n, codec.decode(input, cycle))));
        }

        for (JsonNode counter : config.get("range_counters")) {
            String raw = counter.asText();
            BigInteger big = new BigInteger(raw);
            if (big.bitLength() > 63) {
                // 2^63 and 2^64 don't fit a signed long, so they can't even
                // be passed to encode(long) — Java's type system covers them.
                continue;
            }
            long n = big.longValueExact();
            tests.add(DynamicTest.dynamicTest("encode(" + raw + ") rejected (out of range)",
                    () -> assertThrows(CounterRangeException.class, () -> codec.encode(n))));
        }

        String probe = codec.encode(0);
        for (JsonNode cycleNode : config.get("invalid_cycles")) {
            String raw = cycleNode.asText();
            BigInteger big = new BigInteger(raw);
            if (big.bitLength() > 63) {
                // Out-of-long-range cycles can't be passed to
                // decode(String, long); Java's type system covers them.
                continue;
            }
            long cycle = big.longValueExact();
            tests.add(DynamicTest.dynamicTest(
                    "decode(probe, " + raw + ") rejected (cycle out of range)",
                    () -> assertThrows(CounterRangeException.class,
                            () -> codec.decode(probe, cycle))));
        }

        return DynamicContainer.dynamicContainer(name, tests);
    }

    @TestFactory
    List<DynamicNode> v1cInvalidConfigs() {
        JsonNode root = TestVectors.load("v1c.json");
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
    private static CyclingDealcode buildInvalidCodec(JsonNode config) {
        CyclingDealcode.Builder builder = CyclingDealcode.builder();
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
        if (config.has("length")) {
            builder.length(config.get("length").asInt());
        }
        if (config.has("domain")) {
            builder.domain(config.get("domain").asText());
        }
        return builder.build();
    }

    private static CyclingDealcode buildCodec(JsonNode config) {
        CyclingDealcode.Builder builder = CyclingDealcode.builder();
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
                .length(config.get("length").asInt())
                .domain(config.get("domain").asText())
                .build();
    }
}
