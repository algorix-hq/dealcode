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
dealcode_encode(dc, 42, code, sizeof code);   /* -> e.g. "4b71b7" */

uint64_t n;
dealcode_decode(dc, code, &n);                /* -> 42 */

dealcode_free(dc);
```

See `include/dealcode.h` for the full, documented API:

- `dealcode_new` / `dealcode_free` — codec lifecycle (opaque handle,
  no global state).
- `dealcode_encode` / `dealcode_decode` — the counter <-> code bijection.
- `dealcode_capacity`, `dealcode_min_length`, `dealcode_max_length`,
  `dealcode_radix`, `dealcode_alphabet` — introspection.
- `dealcode_strerror` — human-readable error descriptions.

Errors are explicit `dealcode_err_t` return codes; on failure nothing is
written to output parameters (except `*out = NULL` in `dealcode_new`).

## Thread safety

A `dealcode_t` is immutable after construction and may be shared freely
across threads. `dealcode_encode`/`dealcode_decode` allocate a fresh OpenSSL
cipher context per call, so concurrent calls on one handle are safe.

## Testing

`make test` regenerates `build/vectors.inc` from
[`../testvectors/`](../testvectors) via `tests/gen_vectors.py` and runs
`tests/test_dealcode.c`, which covers the 9 official NIST FF1 sample vectors
(through the private FF1 seam in `src/ff1.h`), every dealcode v1 test-vector
config, error behaviour, and large roundtrip sweeps across stage boundaries —
including configurations where `radix^max_length` is exactly `2^128`.

## License

MIT — see [LICENSE](../LICENSE).
