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
- CMake 3.16+, plus `python3` to generate test vectors for the test suite
  (only when `DEALCODE_BUILD_TESTS` is on; consumers never need it).

## Build and test

```sh
cmake -S cpp -B cpp/build
cmake --build cpp/build
ctest --test-dir cpp/build
```

The CMake project builds the C core (`dealcode_core`, from
`../c/src/dealcode.c`), exposes the wrapper as the `dealcode::dealcode`
target, and registers the test binary with CTest. Tests (and their Python3
requirement) are gated behind `DEALCODE_BUILD_TESTS`, which defaults to ON
only when this project is the top-level build — consumers never need
python3.

## Consuming from another CMake project

All three modes link the same target name, `dealcode::dealcode`, and only
need OpenSSL installed:

**`add_subdirectory`** (vendored checkout; tests off automatically because
the project is not top-level):

```cmake
add_subdirectory(path/to/dealcode/cpp dealcode)
target_link_libraries(myapp PRIVATE dealcode::dealcode)
```

**FetchContent**:

```cmake
include(FetchContent)
FetchContent_Declare(dealcode
    GIT_REPOSITORY https://github.com/algorix-hq/dealcode.git
    GIT_TAG v1.0.1
    SOURCE_SUBDIR cpp)
FetchContent_MakeAvailable(dealcode)
target_link_libraries(myapp PRIVATE dealcode::dealcode)
```

**`find_package` after installing** (installs the static core, both
headers, and CMake package files, so downstream builds need no source
checkout):

```sh
cmake -S cpp -B cpp/build && cmake --build cpp/build
cmake --install cpp/build --prefix /usr/local    # or any prefix
```

```cmake
find_package(dealcode 1.0 REQUIRED)   # CMAKE_PREFIX_PATH must reach the prefix
target_link_libraries(myapp PRIVATE dealcode::dealcode)
```

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

std::string code = codec.encode(42);          // e.g. "59e5f2"
uint64_t n = codec.decode(code);              // 42

codec.capacity();    // min(radix^max_length, 2^63)
codec.min_length(); codec.max_length(); codec.radix(); codec.alphabet();
```

## Fixed-length cycling mode

For code shapes that must never grow (airline-PNR-style fixed-length
codes), [SPEC.md §11](../SPEC.md#11-fixed-length-cycling-mode-v1c) defines
a cycling mode, wrapped by `dealcode::CycleCodec`: codes are always exactly
`length` characters, and the counter space `[0, 2^63)` is used in *cycles*
of `capacity() = radix^length` codes — each cycle refills the same code
space through a different keyed permutation (a different FF1 tweak,
namespace `dealcode/v1c/`).

```cpp
dealcode::CycleOptions opts;
opts.alphabet = "crockford";
opts.length = 6;              // 2 <= length <= 128, 100 <= radix^length <= 2^63
opts.domain = "bookings";

dealcode::CycleCodec codec("example-key", opts);  // same key rules as Codec

uint64_t n = next_counter();                 // e.g. 3000000007
uint64_t cycle = n / codec.capacity();

std::string code = codec.encode(n);          // always exactly 6 chars
uint64_t back = codec.decode(code, cycle);   // back == n — cycle is required

codec.capacity();   // codes per cycle: radix^length
codec.max_cycle();  // largest usable cycle: (2^63 - 1) / capacity()
```

`encode` throws `RangeError` for `n >= 2^63`; `decode` throws `RangeError`
for `cycle > max_cycle()` and `InvalidCodeError` for bad codes (including
codes that would map past `2^63` in the final partial cycle). `CycleCodec`
has the same move-only/thread-safety semantics as `Codec`.

**Operational rule (SPEC.md §11.3):** codes *repeat* across cycles by
design. Keep at most one cycle's codes live per uniqueness scope — retire
or expire cycle `e`'s codes before issuing from cycle `e + 1`, use
`UNIQUE(cycle, code)` rather than `UNIQUE(code)`, and persist which cycle
each live code belongs to: `decode` requires it, and the library cannot
recover the cycle from the code string. Decoding with a wrong (but
in-range) cycle is *not* an error — it silently returns a different
counter; the existence check on the decoded counter is what catches it.

## Errors

All failures throw exceptions rooted at `dealcode::Error`
(itself a `std::runtime_error`):

| Exception                    | Raised when |
|------------------------------|-------------|
| `dealcode::ConfigError`      | invalid key, alphabet, lengths, or domain at construction |
| `dealcode::RangeError`       | `encode(n)` with `n >= capacity()` |
| `dealcode::InvalidCodeError` | `decode` input fails length/charset/stage-range checks |

Out-of-memory conditions throw `std::bad_alloc`.

Construction failures carry the C core's field-level diagnostic
(`dealcode_new_ex`) in `what()`, e.g.
`Codec(): alphabet: duplicate character 'a'` or
`Codec(): string key "crockford" is a preset alphabet name — did you swap
the key and alphabet fields?`.

## Database integration

dealcode only needs a never-repeating integer, which your database already
produces: create a `bigint` sequence (or an identity/`AUTO_INCREMENT`
column), fetch `nextval` through your driver (libpqxx, MySQL Connector/C++,
soci, ...), and pass it to `codec.encode(n)` — a pure computation, no locks
or extra round trips. The full recipe and rationale live in the
[root README's "Wiring it to a database"](../README.md#wiring-it-to-a-database).

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
v1 vector config (including out-of-range counters and invalid configs),
the preset-name guards, construction diagnostics, exception behaviour,
move semantics, and roundtrip sweeps across stage boundaries. Cycling mode
is covered by every case in `testvectors/v1c.json` plus permutation,
boundary, moved-from, and exception-type behaviour tests.

## License

MIT — see [LICENSE](../LICENSE).
