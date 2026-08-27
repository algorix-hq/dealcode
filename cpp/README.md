# dealcode — C++ library

C++17 wrapper over the [dealcode C core](../c). The algorithm (FF1 per
NIST SP 800-38G plus the dealcode layer, see [SPEC.md](../SPEC.md)) is
implemented once in C; this wrapper adds RAII, exceptions, and
`std::string` ergonomics. It is header-only: include
[`include/dealcode.hpp`](include/dealcode.hpp) and link the C core plus
OpenSSL libcrypto.

## Requirements

- A C++17 compiler (GCC or Clang; the C core uses `unsigned __int128`).
- OpenSSL libcrypto with headers (`libssl-dev` on Debian/Ubuntu).
- CMake 3.16+, plus `python3` to generate test vectors for the test suite.

## Build and test

```sh
cmake -S cpp -B cpp/build
cmake --build cpp/build
ctest --test-dir cpp/build
```

The CMake project builds the C core (`dealcode_core`, from
`../c/src/dealcode.c`), exposes the wrapper as the `dealcode_cpp` interface
library, and registers the test binary with CTest. To consume from another
CMake project, `add_subdirectory(cpp)` and link `dealcode_cpp`.

## Usage

```cpp
#include <dealcode.hpp>

dealcode::Options opts;
opts.alphabet = "hex";        // preset name or custom alphabet chars
opts.min_length = 6;
opts.domain = "orders";       // namespace label bound into the FF1 tweak
// opts.max_length: std::nullopt selects the spec default

dealcode::Codec codec("example-key", opts);   // string key rule (derived)
// bytes rule: dealcode::Codec(std::vector<std::uint8_t>{...}, opts)
//         or: dealcode::Codec(ptr, len, opts)

std::string code = codec.encode(42);          // e.g. "4b71b7"
uint64_t n = codec.decode(code);              // 42

codec.capacity();    // min(radix^max_length, 2^63)
codec.min_length(); codec.max_length(); codec.radix(); codec.alphabet();
```

## Errors

All failures throw exceptions rooted at `dealcode::Error`
(itself a `std::runtime_error`):

| Exception                    | Raised when |
|------------------------------|-------------|
| `dealcode::ConfigError`      | invalid key, alphabet, lengths, or domain at construction |
| `dealcode::RangeError`       | `encode(n)` with `n >= capacity()` |
| `dealcode::InvalidCodeError` | `decode` input fails length/charset/stage-range checks |

Out-of-memory conditions throw `std::bad_alloc`.

## Semantics

- `Codec` is **move-only** (it owns the underlying C handle, including key
  material, via `std::unique_ptr` with a custom deleter). Wrap it in a
  `std::shared_ptr<const dealcode::Codec>` if you need shared ownership.
- A `Codec` is immutable after construction; all member functions are
  `const` and safe to call concurrently from multiple threads.

## Testing

`ctest` runs `tests/test_dealcode.cpp`, which regenerates the vector data
from [`../testvectors/`](../testvectors) (via the C library's
`gen_vectors.py`) and covers the official NIST FF1 samples, every dealcode
v1 vector config, exception behaviour, move semantics, and roundtrip sweeps
across stage boundaries.

## License

MIT — see [LICENSE](../LICENSE).
