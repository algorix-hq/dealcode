# dealcode — C library

C11 implementation of [dealcode format version 1](../SPEC.md): a bijective
mapping between integer counters and short, fixed-alphabet, random-looking
codes, built on FF1 format-preserving encryption (NIST SP 800-38G).

## Requirements

- A C11 compiler providing `unsigned __int128` (GCC or Clang).
- OpenSSL libcrypto 1.1+/3.x with headers (`libssl-dev` on Debian/Ubuntu).
  **Consumers must link with `-lcrypto`.**
- `make`, plus `python3` to generate test vectors for the test suite.

## Build

```sh
make            # builds the static library libdealcode.a
make test       # generates test vectors and runs the test suite
make clean
```

Compile against the header in `include/` and link the static library:

```sh
cc -Ic/include myapp.c c/libdealcode.a -lcrypto
```

### Install and pkg-config

`make install` installs the static library, the header, and a generated
`dealcode.pc`, honoring `PREFIX` (default `/usr/local`) and `DESTDIR` for
staged/packaged installs (`LIBDIR`, `INCLUDEDIR`, `PKGCONFIGDIR` can also be
overridden individually):

```sh
make install PREFIX=/usr/local            # may need sudo
make install DESTDIR=/tmp/stage           # staged install for packaging
```

Consumers can then build with pkg-config (`Requires.private: libcrypto`, so
static links add `-lcrypto` via `--static`):

```sh
cc myapp.c $(pkg-config --cflags --libs dealcode) -lcrypto
```

The installed header defines `DEALCODE_VERSION` (`"1.0.0"`), which is also
the `Version` reported by `pkg-config --modversion dealcode`.

### Sanitizers and custom flags

`CFLAGS` only carries optimization/debug flags; the flags the build requires
(`-std=c11`, warnings, `-Iinclude`) are appended via `override`, so
`make test CFLAGS=-g` cannot break the build. `EXTRA_CFLAGS` is appended
last and reaches both compile and test-link steps — the sanitizer seam:

```sh
make test EXTRA_CFLAGS='-fsanitize=address,undefined -fno-sanitize-recover=all -g -O1'
```

## Usage

```c
#include <dealcode.h>

dealcode_config_t cfg = {0};
cfg.key_string = "example-key";   /* string rule: always SHA-256 derived  */
/* or raw bytes:  cfg.key = bytes; cfg.key_len = 32;  (bytes rule)        */
cfg.alphabet   = "hex";           /* preset name or custom alphabet chars */
cfg.min_length = 6;               /* 0 selects the default (6)            */
cfg.max_length = 0;               /* 0 selects the spec default           */
cfg.domain     = "orders";        /* namespace label, bound into the tweak */

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

See `include/dealcode.h` for the full, documented API:

- `dealcode_new` / `dealcode_new_ex` / `dealcode_free` — codec lifecycle
  (opaque handle, no global state). `dealcode_new_ex` additionally writes a
  one-line diagnostic naming the offending field into a caller-supplied
  buffer on failure (e.g. `alphabet: duplicate character 'a'`,
  `min_length 1 < 2`); size it with `DEALCODE_ERRBUF_SIZE`.
- `dealcode_encode` / `dealcode_decode` — the counter <-> code bijection.
- `dealcode_capacity`, `dealcode_min_length`, `dealcode_max_length`,
  `dealcode_radix`, `dealcode_alphabet` — introspection.
- `dealcode_strerror` — human-readable error descriptions.

Errors are explicit `dealcode_err_t` return codes; on failure nothing is
written to output parameters (except `*out = NULL` in `dealcode_new`).

## Fixed-length cycling mode

For code shapes that must never grow (airline-PNR-style fixed-length
codes), [SPEC.md §11](../SPEC.md#11-fixed-length-cycling-mode-v1c) defines
a cycling mode: codes are always exactly `length` characters, and the
counter space `[0, 2^63)` is used in *cycles* of `capacity = radix^length`
codes — each cycle refills the same code space through a different keyed
permutation (a different FF1 tweak, namespace `dealcode/v1c/`).

```c
dealcode_cycle_config_t cfg = {0};
cfg.key_string = "example-key";   /* same key rules as dealcode_config_t  */
cfg.alphabet   = "crockford";     /* same alphabet rules                  */
cfg.length     = 6;               /* fixed code length; 0 selects 6       */
cfg.domain     = "bookings";

dealcode_cycle_t *dc = NULL;
dealcode_cycle_new(&cfg, &dc);    /* or dealcode_cycle_new_ex for errbuf  */

uint64_t n = /* counter from your sequence */ 3000000007ULL;
uint64_t cycle = n / dealcode_cycle_capacity(dc);

char code[DEALCODE_MAX_CODE_SIZE];
dealcode_cycle_encode(dc, n, code, sizeof code);   /* always 6 chars */

uint64_t back;
dealcode_cycle_decode(dc, code, cycle, &back);     /* back == n */

dealcode_cycle_free(dc);
```

Constraints: `2 <= length <= 128`, `radix^length >= 100`, and
`radix^length <= 2^63` (a cycle must be completable; for larger fixed
spaces use the plain codec with `min_length == max_length`).

**Operational rule (SPEC.md §11.3):** codes *repeat* across cycles by
design. Keep at most one cycle's codes live per uniqueness scope — retire
or expire cycle `e`'s codes before issuing from cycle `e + 1`, use
`UNIQUE(cycle, code)` rather than `UNIQUE(code)`, and persist which cycle
each live code belongs to: `dealcode_cycle_decode` requires it, and the
library cannot recover the cycle from the code string.

## Database integration

dealcode only needs a never-repeating integer, which your database already
produces: create a `bigint` sequence (or use an identity/`AUTO_INCREMENT`
column), fetch `nextval` from C through your driver of choice (libpq,
MySQL C API, sqlite3), and pass it to `dealcode_encode` — a pure
computation, no locks or extra round trips. The full recipe (sequence DDL,
why gaps are fine, and why a `UNIQUE` index on the code column is a
tripwire rather than a mechanism) is in the
[root README's "Wiring it to a database"](../README.md#wiring-it-to-a-database).

## Thread safety

A `dealcode_t` is immutable after construction and may be shared freely
across threads. `dealcode_encode`/`dealcode_decode` allocate a fresh OpenSSL
cipher context per call, so concurrent calls on one handle are safe.

## Testing

`make test` regenerates `build/vectors.inc` from
[`../testvectors/`](../testvectors) via `tests/gen_vectors.py` and runs
`tests/test_dealcode.c`, which covers the 9 official NIST FF1 sample vectors
(through the private FF1 seam in `src/ff1.h`), every dealcode v1 test-vector
config (encode/decode pairs, invalid codes, normalization, out-of-range
counters), every invalid-config vector, the preset-name guards for alphabets
and string keys, `dealcode_new_ex` diagnostics, error behaviour, and large
roundtrip sweeps across stage boundaries — including configurations where
`radix^max_length` is exactly `2^128`. Cycling mode is covered by every
case in `testvectors/v1c.json` (vectors, invalid codes/cycles, normalize,
range counters, invalid configs) plus permutation, final-partial-cycle,
tweak-namespace, and maximum-tweak-length behaviour tests.

## License

MIT — see [LICENSE](../LICENSE).
