# Getting started

Every implementation mirrors the same tiny API: construct a codec (key,
alphabet, min/max length, domain), then `encode` / `decode`. The examples
below are taken from each implementation's README — same key material rules,
same outputs, bit-identical across languages.

## Quickstart

=== "Python"

    ```sh
    pip install dealcode
    ```

    Requires Python ≥ 3.9. Only dependency: [`cryptography`](https://cryptography.io) (PyCA).

    ```python
    from dealcode import Dealcode

    codec = Dealcode(key="0a1b...64-hex-chars-from-your-secret-manager")

    codec.encode(0)        # '767a5b'   (6 hex chars)
    codec.encode(1)        # '421163'   never collides with any other counter
    codec.decode("421163") # 1
    ```

=== "TypeScript / JavaScript"

    ```sh
    npm install dealcode
    ```

    Requires Node.js ≥ 18. Zero runtime dependencies (`node:crypto`); ESM +
    CommonJS builds with full TypeScript types.

    ```ts
    import { Dealcode } from "dealcode";

    const codec = new Dealcode({ key: process.env.DEALCODE_KEY! });

    codec.encode(0);        // e.g. '767a5b' (6 hex chars; depends on your key)
    const code = codec.encode(1);    // never collides with any other counter
    codec.decode(code);              // 1n  (bigint — counters can exceed 2^53)
    codec.decodeNumber(code);        // 1   (number; throws if > Number.MAX_SAFE_INTEGER)
    ```

=== "Go"

    ```sh
    go get github.com/algorix-hq/dealcode/go
    ```

    Requires Go ≥ 1.21. Standard library only.

    ```go
    import dealcode "github.com/algorix-hq/dealcode/go"

    codec, err := dealcode.New(dealcode.Config{
    	KeyString: "0a1b...64-hex-chars-from-your-secret-manager",
    })
    if err != nil {
    	log.Fatal(err)
    }

    codec.Encode(0)        // "767a5b", nil   (6 hex chars)
    codec.Encode(1)        // "421163", nil   never collides with any other counter
    codec.Decode("421163") // 1, nil
    ```

=== "Java"

    ```xml
    <dependency>
      <groupId>io.algorix</groupId>
      <artifactId>dealcode</artifactId>
      <version>1.0.1</version>
    </dependency>
    ```

    Requires Java 17+. Zero runtime dependencies (JCE built-in).

    ```java
    import io.algorix.dealcode.Dealcode;

    Dealcode codec = Dealcode.builder()
            .key("0a1b...64-hex-chars-from-your-secret-manager")
            .build();

    codec.encode(0);        // "767a5b"   (6 hex chars)
    codec.encode(1);        // "421163"   never collides with any other counter
    codec.decode("421163"); // 1
    ```

=== "Rust"

    ```sh
    cargo add dealcode
    ```

    Requires Rust ≥ 1.85. Only runtime dependencies: audited RustCrypto
    crates `aes` and `sha2`.

    ```rust
    use dealcode::Dealcode;

    let codec = Dealcode::new("0a1b...64-hex-chars-from-your-secret-manager")?;

    codec.encode(0)?;        // "767a5b"   (6 hex chars)
    codec.encode(1)?;        // "421163"   never collides with any other counter
    codec.decode("421163")?; // 1
    ```

=== "C"

    ```sh
    make            # in c/ — builds the static library libdealcode.a
    cc -Ic/include myapp.c c/libdealcode.a -lcrypto
    ```

    Requires a C11 compiler with `unsigned __int128` (GCC/Clang) and OpenSSL
    libcrypto 1.1+/3.x.

    ```c
    #include <dealcode.h>

    dealcode_config_t cfg = {0};
    cfg.key_string = "example-key";   /* string rule: always SHA-256 derived  */
    cfg.alphabet   = "hex";
    cfg.domain     = "orders";

    dealcode_t *dc = NULL;
    dealcode_err_t err = dealcode_new(&cfg, &dc);
    if (err != DEALCODE_OK) {
        fprintf(stderr, "dealcode: %s\n", dealcode_strerror(err));
        return 1;
    }

    char code[DEALCODE_MAX_CODE_SIZE];
    dealcode_encode(dc, 42, code, sizeof code);   /* -> e.g. "59e5f2" */

    uint64_t n;
    dealcode_decode(dc, code, &n);                /* -> 42 */

    dealcode_free(dc);
    ```

=== "C++"

    ```sh
    cmake -S cpp -B cpp/build && cmake --build cpp/build
    ```

    C++17 header-only wrapper over the C core (RAII, exceptions,
    `std::string`); link the C core plus OpenSSL libcrypto.

    ```cpp
    #include <dealcode.hpp>

    dealcode::Options opts;
    opts.alphabet = "hex";
    opts.domain = "orders";

    dealcode::Codec codec("example-key", opts);   // string key rule (derived)

    std::string code = codec.encode(42);          // e.g. "59e5f2"
    uint64_t n = codec.decode(code);              // 42
    ```

## Keys

The key can be raw bytes (16/24/32 bytes are used as-is as an AES key) or
*any* string/bytes — hex output from `openssl rand -hex 32`, a passphrase, a
KMS blob. Non-AES-sized material is deterministically expanded
(`SHA-256("dealcode/v1/kdf" ‖ material)`), identically in every language.

```sh
openssl rand -hex 32
```

Generate a key once, keep it in your secret manager, and never change it for
a live namespace — the mapping is stable only while the key (and every other
option) stays fixed. Details and footguns: [Configuration](guide/configuration.md).

## Picking a shape

```python
Dealcode(key, "crockford", domain="coupons")       # human-friendly, e.g. '7Q4WKZ'
Dealcode(key, "dec", min_length=8, domain="orders")  # digits only
Dealcode(key, "hex", min_length=16, max_length=16)   # constant-length tokens
```

Every language exposes the same four options — `alphabet`, `min_length`,
`max_length`, `domain` — spelled idiomatically (`minLength` in JS,
`.minLength(...)` on the Java builder, and so on). See
[Configuration](guide/configuration.md) for the full alphabet table and
rules.

## What decode does — and doesn't — prove

`decode` rejects **malformed** input (wrong length, characters outside the
alphabet, value outside the issuable range) with the language's
invalid-code error, before your database is ever touched. But a
*well-formed* code always decodes to some counter, whether or not that
counter was ever issued — inherent to a permutation. Treat decode as
parsing, not proof of existence: look the counter up before acting on it,
and note that a one-character typo in a valid code can resolve to a
*different* valid counter — add rate limiting (and, for human-typed flows,
an existence check or your own check digit).

## Next steps

- Wire it to your database: [Database integration](guide/database.md)
- Alphabets, domains, lengths, key rules: [Configuration](guide/configuration.md)
- What the key does and doesn't protect: [Security model](guide/security.md)
- Coding with an AI agent? `npx skills add algorix-hq/dealcode`, and the
  full docs in one file:
  [llms-full.txt](https://algorix-hq.github.io/dealcode/llms-full.txt)

If your codes must stay exactly the same length forever — even after the
code space fills up — see the
[fixed-length cycling mode](guide/configuration.md#fixed-length-cycling-mode)
in the configuration guide. If they must be integers in a range you choose
(6-digit numbers with no leading zero, say), see the
[integer range mode](guide/configuration.md#integer-range-mode).
