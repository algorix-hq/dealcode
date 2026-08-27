# dealcode (Go)

Collision-free, random-looking codes from a counter. Go implementation of
the [dealcode spec](../SPEC.md).

## Install

```sh
go get github.com/algorix-hq/dealcode/go
```

Requires Go ≥ 1.21. Standard library only — AES and SHA-256 come from
`crypto/aes` and `crypto/sha256`.

Releases are tagged with the `go/` module prefix per the usual monorepo
convention: `go/v1.x.y`.

## Quickstart

```go
import dealcode "github.com/algorix-hq/dealcode/go"

codec, err := dealcode.New(dealcode.Config{
	KeyString: "0a1b...64-hex-chars-from-your-secret-manager",
})
if err != nil {
	log.Fatal(err)
}

codec.Encode(0)        // "d3f8a1", nil   (6 hex chars)
codec.Encode(1)        // "0b47c9", nil   never collides with any other counter
codec.Decode("0b47c9") // 1, nil
```

The key can be raw bytes (`Config.Key`; 16/24/32 bytes are used as-is as an
AES key) or any string (`Config.KeyString`) or other-length bytes, which are
deterministically expanded to an AES-256 key. Generate one with
`openssl rand -hex 32` and keep it in your secret manager — the mapping is
stable only while the key (and every other option) stays fixed.

### Options

```go
dealcode.Config{
	Key:       nil,    // []byte — exactly one of Key / KeyString
	KeyString: "",     // string key material (never auto-hex-decoded)
	Alphabet:  "hex",  // "dec" | "hex" | "base32" | "crockford" | "base36"
	                   // | "base58" | "base62" | "base64url" | custom string
	MinLength: 6,      // codes start at this length... (0 means the default, 6)
	MaxLength: 0,      // ...and grow one char at a time up to this
	                   // (0 means the largest length fully reachable by int64 counters)
	Domain:    "",     // namespace: same key, unrelated codes per domain
}
```

```go
coupon, _ := dealcode.New(dealcode.Config{Key: key, Alphabet: "crockford", Domain: "coupons"}) // human-friendly, e.g. "7Q4WKZ"
order, _ := dealcode.New(dealcode.Config{Key: key, Alphabet: "dec", MinLength: 8, Domain: "orders"}) // digits only
fixed, _ := dealcode.New(dealcode.Config{Key: key, MinLength: 16, MaxLength: 16}) // constant-length hex
```

`Decode` returns an error wrapping `ErrInvalidCode` for anything this codec
never issued; `Encode` returns an error wrapping `ErrRange` outside
`[0, codec.Capacity())`; `New` returns an error wrapping `ErrConfig` for any
invalid configuration. Classify with `errors.Is`.

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

```go
codec, err := dealcode.New(dealcode.Config{
	KeyString: os.Getenv("DEALCODE_KEY"),
	Domain:    "orders",
})

func createOrder(ctx context.Context, db *sql.DB) (string, error) {
	var n int64
	if err := db.QueryRowContext(ctx, "SELECT nextval('order_code_seq')").Scan(&n); err != nil {
		return "", err
	}
	code, err := codec.Encode(n)
	if err != nil {
		return "", err
	}
	_, err = db.ExecContext(ctx, "INSERT INTO orders (id, code) VALUES ($1, $2)", n, code)
	return code, err
}

func findOrder(ctx context.Context, db *sql.DB, code string) (*Order, error) {
	n, err := codec.Decode(code) // no DB roundtrip for obviously-bad codes
	if errors.Is(err, dealcode.ErrInvalidCode) {
		return nil, nil
	}
	// ... SELECT * FROM orders WHERE id = n
}
```

Sequences never hand out the same number twice (even across concurrent
transactions and rollbacks), so codes never collide. Gaps in the sequence are
invisible — codes look random anyway.

If the `UNIQUE` constraint on `code` ever fires, do not retry: it means the
key/config changed for an existing namespace. Investigate.

## Concurrency & performance

A `Codec` is immutable and safe for concurrent use by multiple goroutines
without locking; create one per namespace at startup and reuse it. Encoding is
ten AES-CBC-MAC rounds — single-digit microseconds, O(1) in the counter value,
with all per-length FF1 parameters precomputed at construction.

## License

MIT — see [LICENSE](../LICENSE).
