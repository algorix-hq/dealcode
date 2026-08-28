# dealcode patterns and good/bad pairs

## Contents

- Constructing a codec (all seven languages)
- Key handling: good/bad
- Config lifecycle: good/bad
- Decode handling: good/bad
- Display and parsing: good/bad
- Database wiring
- Cycling mode (fixed-length): good/bad
- Cross-service use

## Constructing a codec (all seven languages)

Presets: `dec, hex, base32, crockford, base36, base58, base62, base64url`;
custom alphabet = 2–94 distinct printable ASCII characters. Errors come in
three kinds everywhere: config (construction), range (encode),
invalid-code (decode).

```python
# Python
codec = Dealcode(key, "crockford", min_length=6, domain="orders")
```

```ts
// TypeScript / JavaScript (counters are bigint-safe; decode returns bigint)
const codec = new Dealcode({ key, alphabet: "crockford", minLength: 6, domain: "orders" });
```

```go
// Go
codec, err := dealcode.New(dealcode.Config{KeyString: key, Alphabet: "crockford", Domain: "orders"})
```

```java
// Java
Dealcode codec = Dealcode.builder().key(key).alphabet("crockford").domain("orders").build();
```

```rust
// Rust
let codec = Dealcode::builder(key).alphabet("crockford").domain("orders").build()?;
```

```c
/* C — dealcode_new_ex gives a field-level diagnostic on failure */
dealcode_config_t cfg = {0};
cfg.key_string = key; cfg.alphabet = "crockford"; cfg.domain = "orders";
char err[DEALCODE_ERRBUF_SIZE];
dealcode_t *dc = NULL;
dealcode_new_ex(&cfg, &dc, err, sizeof err);
```

```cpp
// C++
dealcode::Options opts; opts.alphabet = "crockford"; opts.domain = "orders";
dealcode::Codec codec(key, opts);
```

## Key handling: good/bad

```python
# GOOD: pass the hex string through as-is; the library derives the AES key
codec = Dealcode(key=os.environ["DEALCODE_KEY"])          # "3f2a…" 64 hex chars

# BAD: hand-decoding changes the derived key — a different permutation
codec = Dealcode(key=bytes.fromhex(os.environ["DEALCODE_KEY"]))
```

Both forms "work" — they just disagree with each other. One namespace must
use one form everywhere, forever.

```python
# BAD: alphabet name where the key belongs builds a hex codec keyed by the
# literal string "crockford" — the library rejects this (config error)
codec = Dealcode("crockford")
# GOOD
codec = Dealcode(key, "crockford")
```

## Config lifecycle: good/bad

```python
# GOOD: a new scheme is a new namespace
invites = Dealcode(key, "crockford", domain="invites-v2")

# BAD: "just make the codes longer" on a live namespace — new permutation,
# can collide with already-issued codes
orders = Dealcode(key, "crockford", min_length=8, domain="orders")  # was 6
```

Treat key, alphabet, min/max length, and domain like a database schema
migration that cannot be rolled out: they are fixed at first issuance.

## Decode handling: good/bad

```python
# GOOD: decode, then confirm existence
try:
    n = codec.decode(user_input.strip())
except InvalidCodeError:
    return None
return db.get_order(n)          # None if the counter was never issued

# BAD: trusting decode success — a typo'd valid-looking code decodes to
# SOMEONE ELSE'S counter
n = codec.decode(user_input)
ship_order(n)
```

Catch the library's invalid-code error type (not a bare exception), and in
JS remember `decode` returns a `bigint`.

## Display and parsing: good/bad

```python
# GOOD: strip display formatting before decode
raw = form_value.strip().replace("-", "")
n = codec.decode(raw)

# BAD: decode("H4P-FG6") — separators are rejected, not skipped
n = codec.decode(form_value)
```

Crockford input may arrive lowercase or with `O/I/L` typos — the library
normalizes those; it never removes characters.

## Database wiring

```sql
CREATE SEQUENCE order_code_seq AS bigint MINVALUE 0 START WITH 0;
CREATE TABLE orders (
  id   bigint PRIMARY KEY,   -- the counter
  code text NOT NULL UNIQUE  -- tripwire only: fires ⇒ config changed
);
```

```python
n = db.scalar("SELECT nextval('order_code_seq')")
code = codec.encode(n)
db.execute("INSERT INTO orders (id, code) VALUES (%s, %s)", (n, code))
```

Sequence gaps are fine (codes look random anyway). Do not add
retry-on-unique-violation logic: with a correct setup that index can only
fire on config drift.

## Cycling mode (fixed-length): good/bad

Fixed-length-forever codes (booking/PNR style). Capacity per cycle is
`radix^length`; when a cycle is exhausted the same code space refills in a
different order.

```python
pnr = CyclingDealcode(key, "crockford", length=6, domain="bookings")
cycle, code = pnr.cycle_of(n), pnr.encode(n)
# store BOTH; later:
n = pnr.decode(code, cycle)
```

```sql
-- GOOD                                  -- BAD
UNIQUE (cycle, code)                     UNIQUE (code)  -- fires at rollover
```

```python
# BAD: guessing the cycle — a wrong in-range cycle returns a DIFFERENT
# counter with no error
n = pnr.decode(code, current_cycle)   # code may be from the previous cycle
```

Retire or expire cycle `e`'s rows before issuing from cycle `e+1`.

## Cross-service use

Same key + config = identical codes in all seven languages, so a Python
issuer and a Go redeemer can share one namespace — but only if both use the
identical key form (string vs bytes), alphabet, lengths, and domain. Put
those four values in shared configuration, not per-service defaults.
