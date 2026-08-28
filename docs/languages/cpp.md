# C++

C++17 wrapper over the [C core](c.md) — the algorithm is implemented once in
C; this wrapper adds RAII, exceptions, and `std::string` ergonomics. It is
header-only: include `dealcode.hpp` and link the C core plus OpenSSL
libcrypto.

Source of truth: [`cpp/` on GitHub](https://github.com/algorix-hq/dealcode/tree/main/cpp)
· [full README](https://github.com/algorix-hq/dealcode/blob/main/cpp/README.md)

## Install

Consumed as a CMake subproject — no package registry step:

```sh
cmake -S cpp -B cpp/build
cmake --build cpp/build
ctest --test-dir cpp/build
```

From another CMake project: `add_subdirectory(cpp)` and link the
`dealcode_cpp` interface library (the project builds the C core
`dealcode_core` for you).

## Minimal example

```cpp
#include <dealcode.hpp>

dealcode::Options opts;
opts.alphabet = "hex";        // preset name or custom alphabet chars
opts.min_length = 6;
opts.domain = "orders";       // namespace label bound into the FF1 tweak

dealcode::Codec codec("example-key", opts);   // string key rule (derived)

std::string code = codec.encode(42);          // e.g. "59e5f2"
uint64_t n = codec.decode(code);              // 42
```

## API surface

| Item | Notes |
|------|-------|
| `dealcode::Codec(key, opts)` | key as string, `std::vector<std::uint8_t>`, or `(ptr, len)` |
| `codec.encode(n)` / `codec.decode(code)` | the counter ↔ code bijection |
| `codec.capacity()`, `min_length()`, `max_length()`, `radix()`, `alphabet()` | introspection |
| Exceptions | `dealcode::ConfigError`, `dealcode::RangeError`, `dealcode::InvalidCodeError`, all rooted at `dealcode::Error` (a `std::runtime_error`) |

A `Codec` is **move-only** (it owns the underlying C handle, key material
included, via `std::unique_ptr`); wrap in
`std::shared_ptr<const dealcode::Codec>` for shared ownership. All member
functions are `const` and safe to call concurrently.

## Tests

```sh
ctest --test-dir cpp/build
```

Covers the official NIST FF1 samples, every dealcode v1 vector config,
exception behaviour, move semantics, and roundtrip sweeps across stage
boundaries.
