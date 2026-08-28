# Python

Reference implementation of the [spec](../spec.md) — the shared test vectors
are generated from it. Requires Python ≥ 3.9; the only dependency is
[`cryptography`](https://cryptography.io) (PyCA), used for AES.

Source of truth: [`python/` on GitHub](https://github.com/algorix-hq/dealcode/tree/main/python)
· [full README](https://github.com/algorix-hq/dealcode/blob/main/python/README.md)

## Install

```sh
pip install dealcode
```

## Minimal example

```python
from dealcode import Dealcode

codec = Dealcode(key="0a1b...64-hex-chars-from-your-secret-manager")

codec.encode(0)        # '767a5b'   (6 hex chars)
codec.encode(1)        # '421163'   never collides with any other counter
codec.decode("421163") # 1
```

## API surface

| Item | Notes |
|------|-------|
| `Dealcode(key, alphabet="hex", min_length=6, max_length=None, domain="")` | immutable, thread-safe; create one per namespace and reuse |
| `codec.encode(n) -> str` | raises `RangeError` outside `[0, codec.capacity)` |
| `codec.decode(code) -> int` | raises `InvalidCodeError` for malformed input |
| `CyclingDealcode(key, alphabet, length=6, domain="")` + `cycle_of(n)` | fixed-length cycling mode, SPEC §11 — see [the configuration guide](../guide/configuration.md#fixed-length-cycling-mode) |
| Errors | `ConfigError`, `RangeError`, `InvalidCodeError`, all subclassing `DealcodeError` (a `ValueError`) |

Encoding is ten AES-CBC-MAC rounds — tens of microseconds, O(1) in the
counter value.

## Tests

```sh
pip install -e ./python pytest    # or: export PYTHONPATH=python/src
python -m pytest python/tests
```

Covers the official NIST FF1 sample vectors, every shared cross-language
vector, and behavioural/edge cases.
