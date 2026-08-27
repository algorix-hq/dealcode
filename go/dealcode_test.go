package dealcode_test

import (
	"crypto/sha256"
	"errors"
	"fmt"
	"strings"
	"sync"
	"testing"

	dealcode "github.com/algorix-hq/dealcode/go"
)

func mustNew(t *testing.T, cfg dealcode.Config) *dealcode.Codec {
	t.Helper()
	codec, err := dealcode.New(cfg)
	if err != nil {
		t.Fatalf("New: %v", err)
	}
	return codec
}

var testKey = []byte{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}

func TestRoundtrip(t *testing.T) {
	codec := mustNew(t, dealcode.Config{Key: testKey, Alphabet: "hex", MinLength: 2, MaxLength: 8})

	counters := make([]int64, 0, 6000)
	for n := int64(0); n < 5000; n++ {
		counters = append(counters, n)
	}
	// Stage boundaries: radix^d - 1, radix^d, radix^d + 1 for each stage, plus
	// the very last counter.
	pow := int64(1)
	for d := 1; d <= 8; d++ {
		pow *= 16
		for _, n := range []int64{pow - 1, pow, pow + 1} {
			if n >= 0 && uint64(n) < codec.Capacity() {
				counters = append(counters, n)
			}
		}
	}
	counters = append(counters, int64(codec.Capacity())-1)

	seen := make(map[string]int64, len(counters))
	for _, n := range counters {
		code, err := codec.Encode(n)
		if err != nil {
			t.Fatalf("Encode(%d): %v", n, err)
		}
		if prev, dup := seen[code]; dup && prev != n {
			t.Fatalf("collision: Encode(%d) == Encode(%d) == %q", n, prev, code)
		}
		seen[code] = n

		wantLen := 2
		for p := int64(16 * 16); p <= n; p *= 16 {
			wantLen++
		}
		if len(code) != wantLen {
			t.Errorf("Encode(%d) = %q: length %d, want %d", n, code, len(code), wantLen)
		}

		got, err := codec.Decode(code)
		if err != nil {
			t.Fatalf("Decode(%q): %v", code, err)
		}
		if got != n {
			t.Fatalf("Decode(Encode(%d)) = %d", n, got)
		}
	}
}

func TestDefaults(t *testing.T) {
	codec := mustNew(t, dealcode.Config{Key: testKey})
	if codec.Alphabet() != "0123456789abcdef" {
		t.Errorf("default Alphabet() = %q", codec.Alphabet())
	}
	if codec.Radix() != 16 {
		t.Errorf("default Radix() = %d", codec.Radix())
	}
	if codec.MinLength() != 6 {
		t.Errorf("default MinLength() = %d", codec.MinLength())
	}
	if codec.MaxLength() != 15 {
		t.Errorf("default MaxLength() = %d, want 15", codec.MaxLength())
	}
	if codec.Domain() != "" {
		t.Errorf("default Domain() = %q", codec.Domain())
	}
	if want := uint64(1) << 60; codec.Capacity() != want { // 16^15
		t.Errorf("default Capacity() = %d, want %d", codec.Capacity(), want)
	}

	// Default MaxLength per preset (SPEC.md §2).
	for name, want := range map[string]int{
		"dec": 18, "base32": 12, "crockford": 12, "base36": 12,
		"base58": 10, "base62": 10, "base64url": 10,
	} {
		c := mustNew(t, dealcode.Config{Key: testKey, Alphabet: name})
		if c.MaxLength() != want {
			t.Errorf("%s: default MaxLength() = %d, want %d", name, c.MaxLength(), want)
		}
	}
}

func TestConfigErrors(t *testing.T) {
	cases := map[string]dealcode.Config{
		"no key":               {},
		"empty key bytes":      {Key: []byte{}},
		"both keys":            {Key: testKey, KeyString: "x"},
		"alphabet too short":   {Key: testKey, Alphabet: "a"},
		"alphabet too long":    {Key: testKey, Alphabet: strings.Repeat("x", 95)},
		"alphabet duplicate":   {Key: testKey, Alphabet: "abcda"},
		"alphabet space":       {Key: testKey, Alphabet: "ab cd"},
		"alphabet non-ascii":   {Key: testKey, Alphabet: "abcдef"},
		"alphabet control":     {Key: testKey, Alphabet: "ab\x01cd"},
		"min length 1":         {Key: testKey, MinLength: 1},
		"min length negative":  {Key: testKey, MinLength: -3},
		"domain too small":     {Key: testKey, Alphabet: "01", MinLength: 6}, // 2^6 = 64 < 100
		"max below min":        {Key: testKey, MinLength: 8, MaxLength: 7},
		"max length negative":  {Key: testKey, MaxLength: -1},
		"codespace over 2^128": {Key: testKey, MaxLength: 33}, // 16^33 = 2^132
		"domain over 255":      {Key: testKey, Domain: strings.Repeat("d", 256)},
		"domain invalid utf8":  {Key: testKey, Domain: "\xff\xfe"},
	}
	for name, cfg := range cases {
		if _, err := dealcode.New(cfg); !errors.Is(err, dealcode.ErrConfig) {
			t.Errorf("%s: New() error = %v, want ErrConfig", name, err)
		}
	}
}

func TestKeyMaterialFlexibility(t *testing.T) {
	// 16/24/32-byte keys are used directly; anything else is derived.
	for _, size := range []int{16, 24, 32, 5, 20, 64} {
		key := make([]byte, size)
		for i := range key {
			key[i] = byte(i)
		}
		codec := mustNew(t, dealcode.Config{Key: key})
		code, err := codec.Encode(42)
		if err != nil {
			t.Fatalf("key size %d: Encode: %v", size, err)
		}
		if n, err := codec.Decode(code); err != nil || n != 42 {
			t.Fatalf("key size %d: Decode(%q) = %d, %v", size, code, n, err)
		}
	}

	// A string key derives AES-256 = SHA-256("dealcode/v1/kdf" || material);
	// passing that digest as a 32-byte Key must yield the identical codec.
	material := "correct horse battery staple"
	derived := sha256.Sum256(append([]byte("dealcode/v1/kdf"), material...))
	viaString := mustNew(t, dealcode.Config{KeyString: material})
	viaBytes := mustNew(t, dealcode.Config{Key: derived[:]})
	for _, n := range []int64{0, 1, 12345, 16777216} {
		a, err := viaString.Encode(n)
		if err != nil {
			t.Fatalf("Encode(%d): %v", n, err)
		}
		b, err := viaBytes.Encode(n)
		if err != nil {
			t.Fatalf("Encode(%d): %v", n, err)
		}
		if a != b {
			t.Errorf("Encode(%d): string key %q != derived bytes key %q", n, a, b)
		}
	}
}

func TestDomainSeparation(t *testing.T) {
	a := mustNew(t, dealcode.Config{Key: testKey, Domain: "orders"})
	b := mustNew(t, dealcode.Config{Key: testKey, Domain: "coupons"})
	codeA, err := a.Encode(7)
	if err != nil {
		t.Fatal(err)
	}
	codeB, err := b.Encode(7)
	if err != nil {
		t.Fatal(err)
	}
	if codeA == codeB {
		t.Errorf("domains produced identical code %q", codeA)
	}
}

func TestRangeErrors(t *testing.T) {
	codec := mustNew(t, dealcode.Config{Key: testKey}) // hex, capacity 16^15 = 2^60
	for _, n := range []int64{-1, -1 << 62, 1 << 60, 1<<60 + 1, 1<<63 - 1} {
		if _, err := codec.Encode(n); !errors.Is(err, dealcode.ErrRange) {
			t.Errorf("Encode(%d) error = %v, want ErrRange", n, err)
		}
	}
	if _, err := codec.Encode(1<<60 - 1); err != nil {
		t.Errorf("Encode(capacity-1): %v", err)
	}
}

// TestCapacityBeyondCounterBound covers radix^MaxLength > 2^63: the counter
// space is clamped to 2^63 and decode rejects the surplus code strings.
func TestCapacityBeyondCounterBound(t *testing.T) {
	codec := mustNew(t, dealcode.Config{Key: testKey, MinLength: 16, MaxLength: 16}) // 16^16 = 2^64
	if want := uint64(1) << 63; codec.Capacity() != want {
		t.Fatalf("Capacity() = %d, want 2^63", codec.Capacity())
	}

	last := int64(1<<63 - 1) // capacity - 1
	code, err := codec.Encode(last)
	if err != nil {
		t.Fatalf("Encode(2^63-1): %v", err)
	}
	if len(code) != 16 {
		t.Fatalf("Encode(2^63-1) = %q, want 16 chars", code)
	}
	if n, err := codec.Decode(code); err != nil || n != last {
		t.Fatalf("Decode(%q) = %d, %v; want %d", code, n, err, last)
	}

	// Roughly half of all 16-char hex strings decrypt to v >= 2^63 and must be
	// rejected; the rest must round-trip. With this fixed key the outcome is
	// deterministic.
	invalid := 0
	for i := 0; i < 64; i++ {
		probe := fmt.Sprintf("%016x", uint64(i)*0x0123456789abcdef)
		n, err := codec.Decode(probe)
		switch {
		case err == nil:
			if n < 0 {
				t.Fatalf("Decode(%q) = %d, negative counter", probe, n)
			}
			back, err := codec.Encode(n)
			if err != nil {
				t.Fatalf("Encode(%d): %v", n, err)
			}
			if back != probe {
				t.Errorf("roundtrip mismatch: Decode(%q)=%d but Encode=%q", probe, n, back)
			}
		case errors.Is(err, dealcode.ErrInvalidCode):
			invalid++
		default:
			t.Fatalf("Decode(%q): unexpected error kind: %v", probe, err)
		}
	}
	if invalid == 0 {
		t.Error("expected at least one probe outside the counter space to be rejected")
	}
}

func TestDecodeInvalid(t *testing.T) {
	codec := mustNew(t, dealcode.Config{Key: testKey, MinLength: 4, MaxLength: 6})
	for _, bad := range []string{
		"",         // empty
		"abc",      // too short
		"abcdefa",  // too long
		"12g4",     // char outside hex
		"12 4",     // space
		"12\x0034", // control char (also wrong length)
		"日本語漢字",    // non-ASCII
	} {
		if _, err := codec.Decode(bad); !errors.Is(err, dealcode.ErrInvalidCode) {
			t.Errorf("Decode(%q) error = %v, want ErrInvalidCode", bad, err)
		}
	}
}

func TestConcurrentUse(t *testing.T) {
	codec := mustNew(t, dealcode.Config{Key: testKey, MinLength: 2, MaxLength: 10})
	const goroutines = 8
	const perGoroutine = 500

	var wg sync.WaitGroup
	errCh := make(chan error, goroutines)
	for g := 0; g < goroutines; g++ {
		g := g
		wg.Add(1)
		go func() {
			defer wg.Done()
			for i := 0; i < perGoroutine; i++ {
				n := int64(g*perGoroutine + i)
				code, err := codec.Encode(n)
				if err != nil {
					errCh <- err
					return
				}
				got, err := codec.Decode(code)
				if err != nil {
					errCh <- err
					return
				}
				if got != n {
					errCh <- errors.New("roundtrip mismatch under concurrency")
					return
				}
			}
		}()
	}
	wg.Wait()
	close(errCh)
	for err := range errCh {
		t.Fatal(err)
	}
}

func TestPreset(t *testing.T) {
	chars, ok := dealcode.Preset("crockford")
	if !ok || chars != "0123456789ABCDEFGHJKMNPQRSTVWXYZ" {
		t.Errorf("Preset(crockford) = %q, %v", chars, ok)
	}
	if _, ok := dealcode.Preset("nope"); ok {
		t.Error("Preset(nope) reported ok")
	}
}

func BenchmarkEncode(b *testing.B) {
	codec, err := dealcode.New(dealcode.Config{Key: testKey})
	if err != nil {
		b.Fatal(err)
	}
	b.ReportAllocs()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		if _, err := codec.Encode(int64(i)); err != nil {
			b.Fatal(err)
		}
	}
}

func BenchmarkDecode(b *testing.B) {
	codec, err := dealcode.New(dealcode.Config{Key: testKey})
	if err != nil {
		b.Fatal(err)
	}
	code, err := codec.Encode(123456)
	if err != nil {
		b.Fatal(err)
	}
	b.ReportAllocs()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		if _, err := codec.Decode(code); err != nil {
			b.Fatal(err)
		}
	}
}
