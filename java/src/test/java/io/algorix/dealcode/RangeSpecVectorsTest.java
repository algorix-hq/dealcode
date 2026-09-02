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
 * Conformance to the integer range-mode vectors (testvectors/v1r.json,
 * SPEC §12.5): every config derives the recorded radix and capacity, every
 * vector encodes and decodes exactly, every invalid code (below {@code low},
 * above {@code high}, or in the dead zone) is rejected with
 * {@link InvalidCodeException}, every out-of-range counter is rejected with
 * {@link CounterRangeException}, and every invalid config is rejected with
 * {@link ConfigException} at build time.
 */
class RangeSpecVectorsTest {

    @TestFactory
    List<DynamicNode> v1rConfigs() {
        JsonNode root = TestVectors.load("v1r.json");
        List<DynamicNode> containers = new ArrayList<>();
        for (JsonNode config : root.get("configs")) {
            containers.add(configContainer(config));
        }
        return containers;
    }

    private static DynamicContainer configContainer(JsonNode config) {
        String name = config.get("name").asText();
        RangeDealcode codec = buildCodec(config);
        List<DynamicNode> tests = new ArrayList<>();

        // The recorded radix and capacity are derived values (SPEC §12.2) —
        // a conforming implementation must select the same domain. capacity
        // is a decimal string and may be exactly 2^63, which only BigInteger
        // represents — capacity() is BigInteger for precisely that reason.
        int radix = config.get("radix").asInt();
        BigInteger capacity = new BigInteger(config.get("capacity").asText());
        tests.add(DynamicTest.dynamicTest("radix() == " + radix,
                () -> assertEquals(radix, codec.radix())));
        tests.add(DynamicTest.dynamicTest("capacity() == " + capacity,
                () -> assertEquals(capacity, codec.capacity())));

        for (JsonNode vector : config.get("vectors")) {
            long n = Long.parseLong(vector.get("n").asText());
            long code = Long.parseLong(vector.get("code").asText());
            tests.add(DynamicTest.dynamicTest("encode(" + n + ") == " + code,
                    () -> assertEquals(code, codec.encode(n))));
            tests.add(DynamicTest.dynamicTest("decode(" + code + ") == " + n,
                    () -> assertEquals(n, codec.decode(code))));
        }

        for (JsonNode invalid : config.get("invalid_codes")) {
            String raw = invalid.asText();
            BigInteger big = new BigInteger(raw);
            if (big.bitLength() > 63) {
                // 2^63 and 2^64 don't fit a signed long, so they can't even
                // be passed to decode(long) — Java's type system covers them.
                continue;
            }
            long code = big.longValueExact();
            tests.add(DynamicTest.dynamicTest("decode(" + raw + ") rejected",
                    () -> assertThrows(InvalidCodeException.class,
                            () -> codec.decode(code))));
        }

        for (JsonNode counter : config.get("range_counters")) {
            String raw = counter.asText();
            BigInteger big = new BigInteger(raw);
            if (big.bitLength() > 63) {
                // Out-of-long-range counters can't be passed to
                // encode(long); Java's type system covers them.
                continue;
            }
            long n = big.longValueExact();
            tests.add(DynamicTest.dynamicTest("encode(" + raw + ") rejected (out of range)",
                    () -> assertThrows(CounterRangeException.class, () -> codec.encode(n))));
        }

        return DynamicContainer.dynamicContainer(name, tests);
    }

    @TestFactory
    List<DynamicNode> v1rInvalidConfigs() {
        JsonNode root = TestVectors.load("v1r.json");
        List<DynamicNode> tests = new ArrayList<>();
        for (JsonNode config : root.get("invalid_configs")) {
            String name = config.get("name").asText();
            BigInteger low = new BigInteger(config.get("low").asText());
            BigInteger high = new BigInteger(config.get("high").asText());
            if (low.bitLength() > 63 || high.bitLength() > 63) {
                // 2^63 ("high-at-2pow63") doesn't fit a signed long, so it
                // can't even be passed to the builder — Java's type system
                // covers it. Negative bounds do fit and are kept.
                continue;
            }
            tests.add(DynamicTest.dynamicTest(name + " rejected at build time",
                    () -> assertThrows(ConfigException.class, () -> buildInvalidCodec(config))));
        }
        return tests;
    }

    /**
     * Builds a codec from an {@code invalid_configs} entry, which unlike a
     * valid config has an optional domain.
     */
    private static RangeDealcode buildInvalidCodec(JsonNode config) {
        RangeDealcode.Builder builder = RangeDealcode.builder();
        if (config.has("key_hex")) {
            builder.key(HexFormat.of().parseHex(config.get("key_hex").asText()));
        } else {
            builder.key(config.get("key_string").asText());
        }
        builder.low(Long.parseLong(config.get("low").asText()));
        builder.high(Long.parseLong(config.get("high").asText()));
        if (config.has("domain")) {
            builder.domain(config.get("domain").asText());
        }
        return builder.build();
    }

    private static RangeDealcode buildCodec(JsonNode config) {
        RangeDealcode.Builder builder = RangeDealcode.builder();
        if (config.has("key_hex")) {
            builder.key(HexFormat.of().parseHex(config.get("key_hex").asText()));
        } else {
            builder.key(config.get("key_string").asText());
        }
        return builder
                .low(Long.parseLong(config.get("low").asText()))
                .high(Long.parseLong(config.get("high").asText()))
                .domain(config.get("domain").asText())
                .build();
    }
}
