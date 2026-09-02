package dealcode_test

// Integer range mode (SPEC.md §12): conformance against
// testvectors/v1r.json, plus behaviour tests.

import (
	"encoding/hex"
	"encoding/json"
	"errors"
	"math"
	"os"
	"strconv"
	"testing"

	dealcode "github.com/algorix-hq/dealcode/go"
)

type v1rConfig struct {
	Name      string  `json:"name"`
	KeyHex    *string `json:"key_hex"`
	KeyString *string `json:"key_string"`
	Low       string  `json:"low"`
	High      string  `json:"high"`
	Domain    string  `json:"domain"`
	Radix     int     `json:"radix"`
	M         int     `json:"m"`
	Capacity  string  `json:"capacity"`
	Vectors   []struct {
		N    string `json:"n"`
		Code string `json:"code"`
	} `json:"vectors"`
	InvalidCodes  []string `json:"invalid_codes"`
	RangeCounters []string `json:"range_counters"`
}

type v1rInvalidConfig struct {
	Name      string  `json:"name"`
	KeyHex    *string `json:"key_hex"`
	KeyString *string `json:"key_string"`
	Low       string  `json:"low"`
	High      string  `json:"high"`
	Domain    string  `json:"domain"`
}

type v1rFile struct {
	Spec           string             `json:"spec"`
	Configs        []v1rConfig        `json:"configs"`
	InvalidConfigs []v1rInvalidConfig `json:"invalid_configs"`
}

func loadV1rFile(t *testing.T) v1rFile {
	t.Helper()
	raw, err := os.ReadFile("../testvectors/v1r.json")
	if err != nil {
		t.Fatalf("read vectors: %v", err)
	}
	var file v1rFile
	if err := json.Unmarshal(raw, &file); err != nil {
		t.Fatalf("parse vectors: %v", err)
	}
	if file.Spec != "dealcode/v1r" {
		t.Fatalf("unexpected spec %q", file.Spec)
	}
	if len(file.Configs) == 0 {
		t.Fatal("no configs in v1r.json")
	}
	return file
}

func rangeFor(t *testing.T, c v1rConfig) *dealcode.RangeCodec {
	t.Helper()
	low, err := strconv.ParseInt(c.Low, 10, 64)
	if err != nil {
		t.Fatalf("low %q: %v", c.Low, err)
	}
	high, err := strconv.ParseInt(c.High, 10, 64)
	if err != nil {
		t.Fatalf("high %q: %v", c.High, err)
	}
	cfg := dealcode.RangeConfig{
		Low:    low,
		High:   high,
		Domain: c.Domain,
	}
	switch {
	case c.KeyHex != nil:
		key, err := hex.DecodeString(*c.KeyHex)
		if err != nil {
			t.Fatalf("key_hex: %v", err)
		}
		cfg.Key = key
	case c.KeyString != nil:
		cfg.KeyString = *c.KeyString
	default:
		t.Fatal("config has neither key_hex nor key_string")
	}
	codec, err := dealcode.NewRange(cfg)
	if err != nil {
		t.Fatalf("NewRange(%s): %v", c.Name, err)
	}
	return codec
}

func TestRangeSpecVectors(t *testing.T) {
	for _, c := range loadV1rFile(t).Configs {
		c := c
		t.Run(c.Name, func(t *testing.T) {
			codec := rangeFor(t, c)

			if codec.Radix() != c.Radix {
				t.Errorf("Radix() = %d, want %d", codec.Radix(), c.Radix)
			}
			// capacity may be exactly 2^63 (full-counter-space), which only
			// fits uint64.
			wantCapacity, err := strconv.ParseUint(c.Capacity, 10, 64)
			if err != nil {
				t.Fatalf("capacity %q: %v", c.Capacity, err)
			}
			if codec.Capacity() != wantCapacity {
				t.Errorf("Capacity() = %d, want %d", codec.Capacity(), wantCapacity)
			}

			for _, vec := range c.Vectors {
				n, err := strconv.ParseInt(vec.N, 10, 64)
				if err != nil {
					t.Fatalf("counter %q: %v", vec.N, err)
				}
				want, err := strconv.ParseInt(vec.Code, 10, 64)
				if err != nil {
					t.Fatalf("code %q: %v", vec.Code, err)
				}
				code, err := codec.Encode(n)
				if err != nil {
					t.Errorf("Encode(%d): %v", n, err)
					continue
				}
				if code != want {
					t.Errorf("Encode(%d) = %d, want %d", n, code, want)
				}
				got, err := codec.Decode(code)
				if err != nil {
					t.Errorf("Decode(%d): %v", code, err)
					continue
				}
				if got != n {
					t.Errorf("Decode(%d) = %d, want %d", code, got, n)
				}
			}

			for _, s := range c.InvalidCodes {
				code, err := strconv.ParseInt(s, 10, 64)
				if err != nil {
					// Codes that do not fit int64 ("9223372036854775808"
					// = 2^63, "18446744073709551616" = 2^64) cannot be passed
					// to Decode at all — the int64 code type covers them.
					continue
				}
				n, err := codec.Decode(code)
				if err == nil {
					t.Errorf("Decode(%d) = %d, want error", code, n)
					continue
				}
				if !errors.Is(err, dealcode.ErrInvalidCode) {
					t.Errorf("Decode(%d) error %v does not wrap ErrInvalidCode", code, err)
				}
			}

			for _, s := range c.RangeCounters {
				n, err := strconv.ParseInt(s, 10, 64)
				if err != nil {
					// Counters that do not fit int64 cannot be passed to
					// Encode at all — the int64 counter type covers them.
					// "-1" does parse and must fail below.
					continue
				}
				code, err := codec.Encode(n)
				if err == nil {
					t.Errorf("Encode(%d) = %d, want error", n, code)
					continue
				}
				if !errors.Is(err, dealcode.ErrRange) {
					t.Errorf("Encode(%d) error %v does not wrap ErrRange", n, err)
				}
			}
		})
	}
}

func TestRangeSpecInvalidConfigs(t *testing.T) {
	file := loadV1rFile(t)
	if len(file.InvalidConfigs) == 0 {
		t.Fatal("no invalid_configs in v1r.json")
	}
	for _, c := range file.InvalidConfigs {
		c := c
		t.Run(c.Name, func(t *testing.T) {
			low, errLow := strconv.ParseInt(c.Low, 10, 64)
			high, errHigh := strconv.ParseInt(c.High, 10, 64)
			if errLow != nil || errHigh != nil {
				// Bounds that do not fit int64 ("9223372036854775808" = 2^63
				// in high-at-2pow63) cannot be passed to NewRange at all —
				// the int64 bound type covers them. "-1" does parse and must
				// fail below.
				t.Skipf("bounds [%s, %s] do not fit int64; unrepresentable by construction", c.Low, c.High)
			}
			cfg := dealcode.RangeConfig{
				Low:    low,
				High:   high,
				Domain: c.Domain,
			}
			switch {
			case c.KeyHex != nil:
				key, err := hex.DecodeString(*c.KeyHex)
				if err != nil {
					t.Fatalf("key_hex: %v", err)
				}
				cfg.Key = key
			case c.KeyString != nil:
				cfg.KeyString = *c.KeyString
			default:
				t.Fatal("config has neither key_hex nor key_string")
			}
			codec, err := dealcode.NewRange(cfg)
			if err == nil {
				t.Fatalf("NewRange(%s) = %v, want error", c.Name, codec)
			}
			if !errors.Is(err, dealcode.ErrConfig) {
				t.Fatalf("NewRange(%s) error %v does not wrap ErrConfig", c.Name, err)
			}
		})
	}
}

// A minimal-span range is a full bijection: the 100 counters map to 100
// distinct codes inside [low, low + capacity) and every code decodes back.
func TestRangeFullBijection(t *testing.T) {
	codec, err := dealcode.NewRange(dealcode.RangeConfig{
		KeyString: "k", Low: 1000, High: 1099, // capacity 100 = 10^2, no dead zone
	})
	if err != nil {
		t.Fatal(err)
	}
	if codec.Capacity() != 100 {
		t.Fatalf("Capacity() = %d, want 100", codec.Capacity())
	}
	seen := make(map[int64]bool, 100)
	for n := int64(0); n < 100; n++ {
		code, err := codec.Encode(n)
		if err != nil {
			t.Fatalf("Encode(%d): %v", n, err)
		}
		if code < 1000 || code > 1099 {
			t.Fatalf("Encode(%d) = %d outside [1000, 1099]", n, code)
		}
		if seen[code] {
			t.Fatalf("code %d repeated", code)
		}
		seen[code] = true
		got, err := codec.Decode(code)
		if err != nil {
			t.Fatalf("Decode(%d): %v", code, err)
		}
		if got != n {
			t.Fatalf("Decode(%d) = %d, want %d", code, got, n)
		}
	}
}

// Codes in the dead zone [low + capacity, high] were never issued and must
// be rejected, as must anything outside [low, high]; the last issued code
// low + capacity - 1 still decodes.
func TestRangeDeadZoneRejected(t *testing.T) {
	codec, err := dealcode.NewRange(dealcode.RangeConfig{
		KeyString: "k", Low: 100000, High: 999999, // capacity 96^3 = 884736
	})
	if err != nil {
		t.Fatal(err)
	}
	if codec.Capacity() != 884736 {
		t.Fatalf("Capacity() = %d, want 884736", codec.Capacity())
	}
	if _, err := codec.Decode(100000 + 884736 - 1); err != nil {
		t.Fatalf("Decode(top issued code): %v", err)
	}
	for _, code := range []int64{0, 99999, 100000 + 884736, 999999, 1000000, math.MaxInt64} {
		n, err := codec.Decode(code)
		if err == nil {
			t.Errorf("Decode(%d) = %d, want error", code, n)
			continue
		}
		if !errors.Is(err, dealcode.ErrInvalidCode) {
			t.Errorf("Decode(%d) error %v does not wrap ErrInvalidCode", code, err)
		}
	}
}

// Low, high, and domain are all bound into the FF1 tweak: codecs differing
// in any one of them are unrelated permutations. Code sequences are compared
// relative to each codec's low so the additive offset cannot mask (or fake)
// a difference.
func TestRangeParametersBindThePermutation(t *testing.T) {
	base := dealcode.RangeConfig{KeyString: "k", Low: 100000, High: 999999}
	sameLow := base.Low

	variants := []struct {
		name string
		cfg  dealcode.RangeConfig
	}{
		// Shifting the range keeps the span (and so the derived domain) but
		// changes low in the tweak.
		{"different low", dealcode.RangeConfig{KeyString: "k", Low: base.Low + 1, High: base.High + 1}},
		// N = 899999 still selects 96^3, so only the tweak's high changes.
		{"different high", dealcode.RangeConfig{KeyString: "k", Low: sameLow, High: base.High - 1}},
		{"different domain", dealcode.RangeConfig{KeyString: "k", Low: sameLow, High: base.High, Domain: "other"}},
	}

	ref, err := dealcode.NewRange(base)
	if err != nil {
		t.Fatal(err)
	}
	for _, v := range variants {
		v := v
		t.Run(v.name, func(t *testing.T) {
			codec, err := dealcode.NewRange(v.cfg)
			if err != nil {
				t.Fatal(err)
			}
			if codec.Capacity() != ref.Capacity() {
				t.Fatalf("Capacity() = %d, want %d (variant must keep the derived domain)", codec.Capacity(), ref.Capacity())
			}
			same := true
			for n := int64(0); n < 50; n++ {
				a, err := ref.Encode(n)
				if err != nil {
					t.Fatalf("ref.Encode(%d): %v", n, err)
				}
				b, err := codec.Encode(n)
				if err != nil {
					t.Fatalf("Encode(%d): %v", n, err)
				}
				if a-ref.Low() != b-codec.Low() {
					same = false
					break
				}
			}
			if same {
				t.Errorf("%s produced the same permutation as the base codec", v.name)
			}
		})
	}
}

// Range-mode construction applies the same guards as the plain codec.
func TestRangeConfigGuards(t *testing.T) {
	cases := []struct {
		name string
		cfg  dealcode.RangeConfig
	}{
		{"both keys set", dealcode.RangeConfig{Key: []byte{1}, KeyString: "x", Low: 100000, High: 999999}},
		{"no key", dealcode.RangeConfig{Low: 100000, High: 999999}},
		{"preset name as key", dealcode.RangeConfig{KeyString: "crockford", Low: 100000, High: 999999}},
		{"negative low", dealcode.RangeConfig{KeyString: "k", Low: -1, High: 200}},
		{"low above high", dealcode.RangeConfig{KeyString: "k", Low: 10, High: 9}},
		{"span under 100", dealcode.RangeConfig{KeyString: "k", Low: 0, High: 98}},
		{"zero-value bounds", dealcode.RangeConfig{KeyString: "k"}},
		{"domain over 255 bytes", dealcode.RangeConfig{KeyString: "k", Low: 100000, High: 999999, Domain: string(make([]byte, 256))}},
	}
	for _, tc := range cases {
		tc := tc
		t.Run(tc.name, func(t *testing.T) {
			codec, err := dealcode.NewRange(tc.cfg)
			if err == nil {
				t.Fatalf("NewRange = %v, want error", codec)
			}
			if !errors.Is(err, dealcode.ErrConfig) {
				t.Fatalf("error %v does not wrap ErrConfig", err)
			}
		})
	}
}

// The full-counter-space configuration [0, 2^63 - 1] derives capacity
// exactly 2^63: every non-negative int64 counter is encodable and the top
// counter round-trips.
func TestRangeCapacityExactly2Pow63(t *testing.T) {
	codec, err := dealcode.NewRange(dealcode.RangeConfig{
		KeyString: "k", Low: 0, High: math.MaxInt64, // capacity 128^9 = 2^63
	})
	if err != nil {
		t.Fatal(err)
	}
	if codec.Capacity() != 1<<63 {
		t.Fatalf("Capacity() = %d, want 2^63", codec.Capacity())
	}
	for _, n := range []int64{0, 1, math.MaxInt64} {
		code, err := codec.Encode(n)
		if err != nil {
			t.Fatalf("Encode(%d): %v", n, err)
		}
		got, err := codec.Decode(code)
		if err != nil || got != n {
			t.Fatalf("Decode(%d) = %d, %v, want %d", code, got, err, n)
		}
	}
}
