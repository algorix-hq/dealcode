# Go

Go implementation of the [spec](../spec.md). Requires Go ≥ 1.21. Standard
library only — AES and SHA-256 come from `crypto/aes` and `crypto/sha256`.

Source of truth: [`go/` on GitHub](https://github.com/algorix-hq/dealcode/tree/main/go)
· [full README](https://github.com/algorix-hq/dealcode/blob/main/go/README.md)

## Install

```sh
go get github.com/algorix-hq/dealcode/go
```

This works directly against GitHub — no registry involved. Releases are
tagged with the `go/` module prefix per the usual monorepo convention:
`go/v1.x.y`.

## Minimal example

```go
import dealcode "github.com/algorix-hq/dealcode/go"

codec, err := dealcode.New(dealcode.Config{
	KeyString: "0a1b...64-hex-chars-from-your-secret-manager",
})
if err != nil {
	log.Fatal(err)
}

codec.Encode(0)        // "767a5b", nil   (6 hex chars)
codec.Encode(1)        // "421163", nil   never collides with any other counter
codec.Decode("421163") // 1, nil
```

## API surface

| Item | Notes |
|------|-------|
| `dealcode.New(dealcode.Config{...}) (*Codec, error)` | `Key []byte` or `KeyString string` (exactly one), `Alphabet`, `MinLength`, `MaxLength`, `Domain`; zero values select spec defaults |
| `codec.Encode(n int64) (string, error)` | error wraps `ErrRange` outside `[0, codec.Capacity())` |
| `codec.Decode(code string) (int64, error)` | error wraps `ErrInvalidCode` for malformed input |
| Errors | sentinel values `ErrConfig`, `ErrRange`, `ErrInvalidCode` — classify with `errors.Is` |

A `Codec` is immutable and safe for concurrent use by multiple goroutines
without locking.

## Tests

```sh
cd go && go vet ./... && go test -race ./...
```

Covers the official NIST FF1 sample vectors, every shared cross-language
vector, behavioural cases, and concurrent use under the race detector.
