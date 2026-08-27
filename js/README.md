# dealcode (TypeScript / JavaScript)

Collision-free, random-looking codes from a counter. TypeScript implementation
of the [dealcode spec](../SPEC.md).

## Install

```sh
npm install dealcode
```

Requires Node.js ≥ 18. Zero runtime dependencies — AES and SHA-256 come from
`node:crypto`. Ships ESM and CommonJS builds with full TypeScript types.

## Quickstart

```ts
import { Dealcode } from "dealcode";

const codec = new Dealcode({ key: process.env.DEALCODE_KEY! });

codec.encode(0);        // 'd3f8a1'   (6 hex chars)
codec.encode(1);        // '0b47c9'   never collides with any other counter
codec.decode("0b47c9"); // 1n         (bigint — counters can exceed 2^53)
codec.decodeNumber("0b47c9"); // 1    (number; throws if > Number.MAX_SAFE_INTEGER)
```

The key can be raw bytes (a `Uint8Array`/`Buffer` of 16/24/32 bytes is used
as-is as an AES key) or any string/bytes, which are deterministically expanded
to an AES-256 key. Generate one with `openssl rand -hex 32` and keep it in your
secret manager — the mapping is stable only while the key (and every other
option) stays fixed.

### Options

```ts
new Dealcode({
  key,                  // string | Uint8Array (required)
  alphabet: "hex",      // "dec" | "hex" | "base32" | "crockford" | "base36"
                        // | "base58" | "base62" | "base64url" | custom string
  minLength: 6,         // codes start at this length...
  maxLength: undefined, // ...and grow one char at a time up to this (default: max for 2^63)
  domain: "",           // namespace: same key, unrelated codes per domain
});
```

| Option      | Type                   | Default | Notes |
|-------------|------------------------|---------|-------|
| `key`       | `string \| Uint8Array` | —       | 16/24/32 bytes → direct AES key; anything else (incl. all strings) → derived. Hex strings are **not** auto-decoded. |
| `alphabet`  | `string`               | `"hex"` | Preset name or 2–94 distinct printable ASCII chars (custom). |
| `minLength` | `number`               | `6`     | `≥ 2` and `radix^minLength ≥ 100`. |
| `maxLength` | `number`               | largest `L` with `radix^L ≤ 2^63 − 1` | `≥ minLength`, `radix^maxLength ≤ 2^128`. |
| `domain`    | `string`               | `""`    | ≤ 255 UTF-8 bytes. Bound into the FF1 tweak. |

```ts
const coupon = new Dealcode({ key, alphabet: "crockford", domain: "coupons" }); // human-friendly, e.g. '7Q4WKZ'
const order  = new Dealcode({ key, alphabet: "dec", minLength: 8, domain: "orders" }); // digits only
const fixed  = new Dealcode({ key, minLength: 16, maxLength: 16 });             // constant-length hex
```

Read-only properties: `alphabet`, `radix`, `minLength`, `maxLength`, `domain`,
and `capacity` (a `bigint`: the number of encodable counters,
`min(radix^maxLength, 2^63)`). The preset alphabet strings are exported as
`ALPHABETS`.

`decode` throws `InvalidCodeError` for anything this codec never issued;
`encode` throws `CounterRangeError` outside `[0, codec.capacity)` (and for
non-integer or unsafe `number` inputs — pass a `bigint` for large counters).
Configuration mistakes throw `ConfigError` at construction. All errors extend
`DealcodeError`.

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

```ts
import { Dealcode, InvalidCodeError } from "dealcode";

const codec = new Dealcode({ key: process.env.DEALCODE_KEY!, domain: "orders" });

async function createOrder(db) {
  const { rows: [{ n }] } = await db.query("SELECT nextval('order_code_seq') AS n");
  const code = codec.encode(BigInt(n));
  await db.query("INSERT INTO orders (id, code) VALUES ($1, $2)", [n, code]);
  return code;
}

async function findOrder(db, code) {
  let n;
  try {
    n = codec.decode(code);           // no DB roundtrip for obviously-bad codes
  } catch (err) {
    if (err instanceof InvalidCodeError) return null;
    throw err;
  }
  const { rows } = await db.query("SELECT * FROM orders WHERE id = $1", [n]);
  return rows[0] ?? null;
}
```

Sequences never hand out the same number twice (even across concurrent
transactions and rollbacks), so codes never collide. Gaps in the sequence are
invisible — codes look random anyway.

If the `UNIQUE` constraint on `code` ever fires, do not retry: it means the
key/config changed for an existing namespace. Investigate.

## Keys

Generate a key once and store it like any other production secret:

```sh
openssl rand -hex 32
```

Pass the hex string straight in — string keys are deterministically derived
into an AES-256 key (`SHA-256("dealcode/v1/kdf" ‖ utf8(key))`), so the same
string yields the same codes in every dealcode implementation. Derivation is
domain separation, not password stretching: a passphrase key is exactly as
strong as the passphrase, so prefer ≥ 128-bit random material. If the key
leaks, the issue order of all past codes is revealed; codes are not
authentication tokens (see SPEC §10).

## Concurrency & performance

A `Dealcode` instance is immutable (frozen) and safe for concurrent use;
create one per namespace at startup and reuse it. Per-codec tables (character
maps, radix powers, FF1 round parameters) are precomputed or cached, and
encoding is ten AES-CBC-MAC rounds — microseconds, O(1) in the counter value.

## License

MIT — see [LICENSE](../LICENSE).
