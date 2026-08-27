package io.algorix.dealcode;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

import com.fasterxml.jackson.databind.JsonNode;
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
 * {@link InvalidCodeException}, and every normalization case decodes to its
 * counter.
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

        return DynamicContainer.dynamicContainer(name, tests);
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
