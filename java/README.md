# dealcode (Java)

Collision-free, random-looking codes from a counter. Java implementation of
the [dealcode spec](../SPEC.md).

## Install

```xml
<dependency>
  <groupId>io.algorix</groupId>
  <artifactId>dealcode</artifactId>
  <version>1.0.0</version>
</dependency>
```

Requires Java 17+. **Zero runtime dependencies** — AES and SHA-256 come from
the JDK's built-in JCE providers.

## Quickstart

```java
import io.algorix.dealcode.Dealcode;

Dealcode codec = Dealcode.builder()
        .key("0a1b...64-hex-chars-from-your-secret-manager")
        .build();

codec.encode(0);        // "d3f8a1"   (6 hex chars)
codec.encode(1);        // "0b47c9"   never collides with any other counter
codec.decode("0b47c9"); // 1
```

The key can be raw bytes (16/24/32 bytes are used as-is as an AES key) or any
string/bytes, which are deterministically expanded to an AES-256 key. Generate
one with `openssl rand -hex 32` and keep it in your secret manager — the
mapping is stable only while the key (and every other option) stays fixed.

### Options

```java
Dealcode.builder()
    .key(bytesOrString)   // byte[] | String (required)
    .alphabet("hex")      // "dec" | "hex" | "base32" | "crockford" | "base36"
                          // | "base58" | "base62" | "base64url" | custom string
    .minLength(6)         // codes start at this length...
    .maxLength(15)        // ...and grow one char at a time up to this
                          // (default: max length whose code space fits 2^63)
    .domain("")           // namespace: same key, unrelated codes per domain
    .build();
```

```java
Dealcode coupon = Dealcode.builder().key(key).alphabet("crockford")
        .domain("coupons").build();                       // human-friendly, e.g. "7Q4WKZ"
Dealcode order = Dealcode.builder().key(key).alphabet("dec")
        .minLength(8).domain("orders").build();           // digits only
Dealcode fixed = Dealcode.builder().key(key).alphabet("hex")
        .minLength(16).maxLength(16).build();             // constant-length
```

The preset alphabet strings are exposed as constants on
`io.algorix.dealcode.Alphabets`.

`decode` throws `InvalidCodeException` for **malformed** input — wrong
length, characters outside the alphabet, or a value outside the issuable
range. A *well-formed* code always decodes to some counter, whether or not
that counter was ever issued (inherent to a permutation — see SPEC §7). Treat
decode as parsing, not proof of existence: look the counter up before acting
on it, and note that a one-character typo in a valid code can resolve to a
*different* valid counter — add rate limiting (and, for human-typed flows, an
existence check or your own check digit).
`encode` throws `CounterRangeException` outside `[0, codec.maxCounter()]`.
All errors subclass `DealcodeException` (an `IllegalArgumentException`).
`maxCounter()` is the inclusive maximum — the counter space may be exactly
2^63 (e.g. 16-char hex), which doesn't fit a `long` as a count, but its
maximum always does.

## Using it with your database

Dealcode does not talk to your database — it only turns a counter into a code.
Any source of never-repeating integers works. With PostgreSQL:

```sql
CREATE SEQUENCE order_code_seq AS bigint MINVALUE 0 START WITH 0;

CREATE TABLE orders (
  id   bigint PRIMARY KEY,          -- the counter
  code text NOT NULL UNIQUE,        -- safety net; alerts on config mistakes
  ...
);
```

```java
Dealcode codec = Dealcode.builder()
        .key(System.getenv("DEALCODE_KEY"))
        .domain("orders")
        .build();

String createOrder(Connection conn) throws SQLException {
    long n;
    try (ResultSet rs = conn.createStatement()
            .executeQuery("SELECT nextval('order_code_seq')")) {
        rs.next();
        n = rs.getLong(1);
    }
    String code = codec.encode(n);
    try (PreparedStatement ps =
            conn.prepareStatement("INSERT INTO orders (id, code) VALUES (?, ?)")) {
        ps.setLong(1, n);
        ps.setString(2, code);
        ps.executeUpdate();
    }
    return code;
}

Optional<Order> findOrder(Connection conn, String code) {
    long n;
    try {
        n = codec.decode(code);      // malformed codes never reach the DB
    } catch (InvalidCodeException e) {
        return Optional.empty();
    }
    return findOrderById(conn, n);
}
```

Sequences never hand out the same number twice (even across concurrent
transactions and rollbacks), so codes never collide. Gaps in the sequence are
invisible — codes look random anyway.

If the `UNIQUE` constraint on `code` ever fires, do not retry: it means the
key/config changed for an existing namespace. Investigate.

## Thread safety & performance

A `Dealcode` instance is immutable and thread-safe — the underlying AES
`Cipher` is kept per-thread in a `ThreadLocal` — so create one per namespace at
startup and share it freely. Encoding is ten AES-CBC-MAC rounds — a few
microseconds, O(1) in the counter value.

## Building & tests

```sh
mvn test    # runs the NIST FF1 vectors, the shared spec vectors, and behavioural tests
```

The conformance tests read `../testvectors/` from the repository root.

## License

MIT — see [LICENSE](../LICENSE).
