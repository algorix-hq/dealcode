# dealcode (Python)

Collision-free, random-looking codes from a counter. Python implementation of
the [dealcode spec](https://github.com/algorix-hq/dealcode/blob/main/SPEC.md).

## Install

```sh
pip install dealcode
```

Note: v1.0.0 is not yet published to PyPI. Until it is, install from source:

```sh
pip install "dealcode @ git+https://github.com/algorix-hq/dealcode#subdirectory=python"
```

Requires Python ≥ 3.9. The only dependency is [`cryptography`](https://cryptography.io) (PyCA), used for AES.

## Quickstart

```python
from dealcode import Dealcode

codec = Dealcode(key="0a1b...64-hex-chars-from-your-secret-manager")

codec.encode(0)        # '767a5b'   (6 hex chars)
codec.encode(1)        # '421163'   never collides with any other counter
codec.decode("421163") # 1
```

(The outputs shown are the real ones for this exact key and default config —
with your own key every counter maps to a different, but equally stable, code.)

The key can be raw bytes (16/24/32 bytes are used as-is as an AES key) or any
string/bytes, which are deterministically expanded to an AES-256 key. Generate
one with `openssl rand -hex 32` and keep it in your secret manager — the
mapping is stable only while the key (and every other option) stays fixed.

### Options

```python
Dealcode(
    key,                    # bytes | str
    alphabet="hex",         # "dec" | "hex" | "base32" | "crockford" | "base36"
                            # | "base58" | "base62" | "base64url" | custom string
    min_length=6,           # codes start at this length...
    max_length=None,        # ...and grow one char at a time up to this (default: max for 2^63)
    domain="",              # namespace: same key, unrelated codes per domain
)
```

```python
coupon = Dealcode(key, "crockford", domain="coupons")   # human-friendly, e.g. '7Q4WKZ'
order  = Dealcode(key, "dec", min_length=8, domain="orders")  # digits only
fixed  = Dealcode(key, "hex", min_length=16, max_length=16)   # constant-length
```

`decode` raises `InvalidCodeError` for **malformed** input — wrong length,
characters outside the alphabet, or a value outside the issuable range.
A *well-formed* code always decodes to some counter, whether or not that
counter was ever issued (inherent to a permutation — see SPEC §7). Treat
decode as parsing, not proof of existence: look the counter up before acting
on it, and note that a one-character typo in a valid code can resolve to a
*different* valid counter — add rate limiting (and, for human-typed flows, an
existence check or your own check digit). `encode` raises `RangeError`
outside `[0, codec.capacity)`, and construction mistakes (bad key, alphabet,
lengths, domain) raise `ConfigError`. All errors subclass `DealcodeError`
(a `ValueError`).

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

```python
import os

from sqlalchemy import text

from dealcode import Dealcode, InvalidCodeError

codec = Dealcode(key=os.environ["DEALCODE_KEY"], domain="orders")

def create_order(conn) -> str:
    n = conn.execute(text("SELECT nextval('order_code_seq')")).scalar_one()
    code = codec.encode(n)
    conn.execute(
        text("INSERT INTO orders (id, code) VALUES (:id, :code)"),
        {"id": n, "code": code},
    )
    return code

def find_order(conn, code: str):
    try:
        n = codec.decode(code)          # malformed codes never reach the DB
    except InvalidCodeError:
        return None
    return conn.execute(text("SELECT * FROM orders WHERE id = :id"), {"id": n}).first()
```

Sequences never hand out the same number twice (even across concurrent
transactions and rollbacks), so codes never collide. Gaps in the sequence are
invisible — codes look random anyway.

If the `UNIQUE` constraint on `code` ever fires, do not retry: it means the
key/config changed for an existing namespace. Investigate.

## Fixed-length cycling mode

For code shapes that must never grow — airline-PNR-style fixed-length codes —
`CyclingDealcode` (SPEC §11) fills the entire fixed-length space, and when it
is exhausted refills the *same* space through a different permutation instead
of adding a character:

```python
from dealcode import CyclingDealcode

pnr = CyclingDealcode(key, "crockford", length=6, domain="bookings")

code = pnr.encode(n)              # always exactly 6 chars; cycle = n // pnr.capacity
n = pnr.decode(code, cycle=3)     # the cycle is required context
```

Codes **repeat across cycles** (the space is being reused — that's the
point), so keep at most one cycle's codes live per uniqueness scope: retire
or expire cycle `e` before issuing from `e+1`, index with
`UNIQUE(cycle, code)` rather than `UNIQUE(code)`, and store each live code's
cycle — `decode` needs it.

## Thread safety & performance

A `Dealcode` instance is immutable and thread-safe; create one per namespace at
startup and reuse it. Encoding is ten AES-CBC-MAC rounds — tens of microseconds,
no allocation-heavy paths, O(1) in the counter value.

## Running the tests

From the repository root:

```sh
pip install -e ./python pytest    # or: export PYTHONPATH=python/src
python -m pytest python/tests
```

The suite covers the official NIST FF1 sample vectors, every shared
cross-language vector in
[`testvectors/`](https://github.com/algorix-hq/dealcode/tree/main/testvectors),
and behavioural/edge cases.

## License

MIT — see [LICENSE](https://github.com/algorix-hq/dealcode/blob/main/LICENSE).
