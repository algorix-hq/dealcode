# Java

Java implementation of the [spec](../spec.md). Requires Java 17+. Zero
runtime dependencies — AES and SHA-256 come from the JDK's built-in JCE
providers.

Source of truth: [`java/` on GitHub](https://github.com/algorix-hq/dealcode/tree/main/java)
· [full README](https://github.com/algorix-hq/dealcode/blob/main/java/README.md)

## Install

```xml
<dependency>
  <groupId>io.algorix</groupId>
  <artifactId>dealcode</artifactId>
  <version>1.0.0</version>
</dependency>
```

Not on Maven Central yet — until then, build from a checkout
(`mvn install` in `java/`) to publish to your local repository.

## Minimal example

```java
import io.algorix.dealcode.Dealcode;

Dealcode codec = Dealcode.builder()
        .key("0a1b...64-hex-chars-from-your-secret-manager")
        .build();

codec.encode(0);        // "767a5b"   (6 hex chars)
codec.encode(1);        // "421163"   never collides with any other counter
codec.decode("421163"); // 1
```

## API surface

| Item | Notes |
|------|-------|
| `Dealcode.builder().key(...).alphabet(...).minLength(...).maxLength(...).domain(...).build()` | key accepts `byte[]` or `String` |
| `codec.encode(long n)` | throws `CounterRangeException` outside `[0, codec.maxCounter()]` (inclusive max — the space may be exactly 2^63) |
| `codec.decode(String code)` | throws `InvalidCodeException` for malformed input |
| `Alphabets` | preset alphabet strings as constants |
| Errors | `ConfigException`, `CounterRangeException`, `InvalidCodeException` — all subclassing `DealcodeException` (an `IllegalArgumentException`) |

A `Dealcode` instance is immutable and thread-safe (the AES `Cipher` is kept
per-thread in a `ThreadLocal`) — create one per namespace and share it.

## Tests

```sh
cd java && mvn test
```

Runs the NIST FF1 vectors, the shared spec vectors (read from
`../testvectors/`), and behavioural tests.
