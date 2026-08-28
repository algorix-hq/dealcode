package io.algorix.dealcode;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.io.IOException;
import java.io.UncheckedIOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;

/** Locates and loads the shared JSON test vectors at the repository root. */
final class TestVectors {

    private TestVectors() {
    }

    /**
     * Finds the repository's {@code testvectors/} directory by walking up
     * from the Maven project dir (or cwd), so tests work regardless of where
     * the build is invoked from.
     */
    static Path dir() {
        String[] starts = {
            System.getProperty("dealcode.testvectors"), // explicit override
            System.getProperty("basedir"),              // set by Maven/Surefire
            System.getProperty("user.dir"),
        };
        for (String start : starts) {
            if (start == null) {
                continue;
            }
            Path p = Paths.get(start).toAbsolutePath().normalize();
            for (int i = 0; i < 8 && p != null; i++, p = p.getParent()) {
                Path tv = p.resolve("testvectors");
                if (Files.isRegularFile(tv.resolve("ff1_nist.json"))
                        && Files.isRegularFile(tv.resolve("v1.json"))
                        && Files.isRegularFile(tv.resolve("v1c.json"))) {
                    return tv;
                }
            }
        }
        throw new IllegalStateException(
                "testvectors/ directory not found above " + System.getProperty("user.dir")
                        + "; run tests from within the repository or set -Ddealcode.testvectors");
    }

    static JsonNode load(String fileName) {
        try {
            return new ObjectMapper().readTree(dir().resolve(fileName).toFile());
        } catch (IOException e) {
            throw new UncheckedIOException("failed to read test vectors: " + fileName, e);
        }
    }
}
