# C

C11 implementation of the [spec](../spec.md) — also the core that the
[C++ wrapper](cpp.md) builds on. Requires GCC or Clang (uses
`unsigned __int128`) and OpenSSL libcrypto 1.1+/3.x; consumers link with
`-lcrypto`.

Source of truth: [`c/` on GitHub](https://github.com/algorix-hq/dealcode/tree/main/c)
· [full README](https://github.com/algorix-hq/dealcode/blob/main/c/README.md)

## Install

Vendored / static library by design — there is no package registry step:

```sh
make            # in c/ — builds the static library libdealcode.a
make test       # generates test vectors and runs the test suite

cc -Ic/include myapp.c c/libdealcode.a -lcrypto
```

## Minimal example

```c
#include <dealcode.h>

dealcode_config_t cfg = {0};
cfg.key_string = "example-key";   /* string rule: always SHA-256 derived  */
cfg.alphabet   = "hex";           /* preset name or custom alphabet chars */
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

## API surface

| Item | Notes |
|------|-------|
| `dealcode_new` / `dealcode_free` | codec lifecycle — opaque handle, no global state |
| `dealcode_encode` / `dealcode_decode` | the counter ↔ code bijection |
| `dealcode_capacity`, `dealcode_min_length`, `dealcode_max_length`, `dealcode_radix`, `dealcode_alphabet` | introspection |
| `dealcode_strerror` | human-readable error descriptions |
| Errors | explicit `dealcode_err_t` return codes; on failure nothing is written to output parameters (except `*out = NULL` in `dealcode_new`) |

The full, documented API lives in `include/dealcode.h`. A `dealcode_t` is
immutable after construction and may be shared freely across threads —
encode/decode allocate a fresh OpenSSL cipher context per call.

## Tests

```sh
cd c && make test
```

Covers the 9 official NIST FF1 sample vectors, every dealcode v1
test-vector config, error behaviour, and large roundtrip sweeps across stage
boundaries — including configurations where `radix^max_length` is exactly
`2^128`.
