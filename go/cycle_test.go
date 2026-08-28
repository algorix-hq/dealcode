package dealcode_test

// Fixed-length cycling mode (SPEC.md §11): conformance against
// testvectors/v1c.json, plus behaviour tests.

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

type v1cConfig struct {
	Name           string  `json:"name"`
	Alphabet       string  `json:"alphabet"`
	CustomAlphabet string  `json:"custom_alphabet"`
	KeyHex         *string `json:"key_hex"`
	KeyString      *string `json:"key_string"`
	Length         int     `json:"length"`
	Domain         string  `json:"domain"`
	Capacity       string  `json:"capacity"`
	MaxCycle       string  `json:"max_cycle"`
	Vectors        []struct {
		N    string `json:"n"`
		Code string `json:"code"`
	} `json:"vectors"`
	InvalidCodes []struct {
		Cycle string `json:"cycle"`
		Code  string `json:"code"`
	} `json:"invalid_codes"`
	Normalize []struct {
		Cycle string `json:"cycle"`
		Input string `json:"input"`
		N     string `json:"n"`
	} `json:"normalize"`
	RangeCounters []string `json:"range_counters"`
	InvalidCycles []string `json:"invalid_cycles"`
}

type v1cInvalidConfig struct {
	Name           string  `json:"name"`
	Alphabet       string  `json:"alphabet"`
	CustomAlphabet string  `json:"custom_alphabet"`
	KeyHex         *string `json:"key_hex"`
	KeyString      *string `json:"key_string"`
	Length         int     `json:"length"`
	Domain         string  `json:"domain"`
}

type v1cFile struct {
	Spec           string             `json:"spec"`
	Configs        []v1cConfig        `json:"configs"`
	InvalidConfigs []v1cInvalidConfig `json:"invalid_configs"`
}

func loadV1cFile(t *testing.T) v1cFile {
	t.Helper()
	raw, err := os.ReadFile("../testvectors/v1c.json")
	if err != nil {
		t.Fatalf("read vectors: %v", err)
	}
	var file v1cFile
	if err := json.Unmarshal(raw, &file); err != nil {
		t.Fatalf("parse vectors: %v", err)
	}
	if file.Spec != "dealcode/v1c" {
		t.Fatalf("unexpected spec %q", file.Spec)
	}
	if len(file.Configs) == 0 {
		t.Fatal("no configs in v1c.json")
	}
	return file
}

func cyclingFor(t *testing.T, c v1cConfig) *dealcode.CycleCodec {
	t.Helper()
	cfg := dealcode.CyclingConfig{
		Length: c.Length,
		Domain: c.Domain,
	}
	if c.Alphabet == "custom" {
		cfg.Alphabet = c.CustomAlphabet
	} else {
		cfg.Alphabet = c.Alphabet
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
	codec, err := dealcode.NewCycling(cfg)
	if err != nil {
		t.Fatalf("NewCycling(%s): %v", c.Name, err)
	}
	return codec
}

func TestCyclingSpecVectors(t *testing.T) {
	for _, c := range loadV1cFile(t).Configs {
		c := c
		t.Run(c.Name, func(t *testing.T) {
			codec := cyclingFor(t, c)

			// capacity may be exactly 2^63 (octal-21-single-cycle), which
			// only fits uint64.
			wantCapacity, err := strconv.ParseUint(c.Capacity, 10, 64)
			if err != nil {
				t.Fatalf("capacity %q: %v", c.Capacity, err)
			}
			if codec.Capacity() != wantCapacity {
				t.Errorf("Capacity() = %d, want %d", codec.Capacity(), wantCapacity)
			}
			wantMaxCycle, err := strconv.ParseInt(c.MaxCycle, 10, 64)
			if err != nil {
				t.Fatalf("max_cycle %q: %v", c.MaxCycle, err)
			}
			if codec.MaxCycle() != wantMaxCycle {
				t.Errorf("MaxCycle() = %d, want %d", codec.MaxCycle(), wantMaxCycle)
			}

			for _, vec := range c.Vectors {
				n, err := strconv.ParseInt(vec.N, 10, 64)
				if err != nil {
					t.Fatalf("counter %q: %v", vec.N, err)
				}
				code, err := codec.Encode(n)
				if err != nil {
					t.Errorf("Encode(%d): %v", n, err)
					continue
				}
				if code != vec.Code {
					t.Errorf("Encode(%d) = %q, want %q", n, code, vec.Code)
				}
				cycle, err := codec.CycleOf(n)
				if err != nil {
					t.Errorf("CycleOf(%d): %v", n, err)
					continue
				}
				got, err := codec.Decode(vec.Code, cycle)
				if err != nil {
					t.Errorf("Decode(%q, %d): %v", vec.Code, cycle, err)
					continue
				}
				if got != n {
					t.Errorf("Decode(%q, %d) = %d, want %d", vec.Code, cycle, got, n)
				}
			}

			for _, bad := range c.InvalidCodes {
				cycle, err := strconv.ParseInt(bad.Cycle, 10, 64)
				if err != nil {
					t.Fatalf("cycle %q: %v", bad.Cycle, err)
				}
				n, err := codec.Decode(bad.Code, cycle)
				if err == nil {
					t.Errorf("Decode(%q, %d) = %d, want error", bad.Code, cycle, n)
					continue
				}
				if !errors.Is(err, dealcode.ErrInvalidCode) {
					t.Errorf("Decode(%q, %d) error %v does not wrap ErrInvalidCode", bad.Code, cycle, err)
				}
			}

			for _, norm := range c.Normalize {
				cycle, err := strconv.ParseInt(norm.Cycle, 10, 64)
				if err != nil {
					t.Fatalf("cycle %q: %v", norm.Cycle, err)
				}
				n, err := strconv.ParseInt(norm.N, 10, 64)
				if err != nil {
					t.Fatalf("counter %q: %v", norm.N, err)
				}
				got, err := codec.Decode(norm.Input, cycle)
				if err != nil {
					t.Errorf("Decode(%q, %d): %v", norm.Input, cycle, err)
					continue
				}
				if got != n {
					t.Errorf("Decode(%q, %d) = %d, want %d", norm.Input, cycle, got, n)
				}
			}

			for _, s := range c.RangeCounters {
				n, err := strconv.ParseInt(s, 10, 64)
				if err != nil {
					// Counters that do not fit int64 ("9223372036854775808"
					// = 2^63, "18446744073709551616" = 2^64) cannot be
					// passed to Encode at all — the int64 counter type
					// covers them. "-1" does parse and must fail below.
					continue
				}
				code, err := codec.Encode(n)
				if err == nil {
					t.Errorf("Encode(%d) = %q, want error", n, code)
					continue
				}
				if !errors.Is(err, dealcode.ErrRange) {
					t.Errorf("Encode(%d) error %v does not wrap ErrRange", n, err)
				}
				if _, err := codec.CycleOf(n); !errors.Is(err, dealcode.ErrRange) {
					t.Errorf("CycleOf(%d) error %v does not wrap ErrRange", n, err)
				}
			}

			probe, err := codec.Encode(0)
			if err != nil {
				t.Fatalf("Encode(0): %v", err)
			}
			for _, s := range c.InvalidCycles {
				cycle, err := strconv.ParseInt(s, 10, 64)
				if err != nil {
					t.Fatalf("invalid cycle %q does not fit int64: %v", s, err)
				}
				n, err := codec.Decode(probe, cycle)
				if err == nil {
					t.Errorf("Decode(%q, %d) = %d, want error", probe, cycle, n)
					continue
				}
				if !errors.Is(err, dealcode.ErrRange) {
					t.Errorf("Decode(%q, %d) error %v does not wrap ErrRange", probe, cycle, err)
				}
			}
		})
	}
}

func TestCyclingSpecInvalidConfigs(t *testing.T) {
	file := loadV1cFile(t)
	if len(file.InvalidConfigs) == 0 {
		t.Fatal("no invalid_configs in v1c.json")
	}
	for _, c := range file.InvalidConfigs {
		c := c
		t.Run(c.Name, func(t *testing.T) {
			cfg := dealcode.CyclingConfig{
				Length: c.Length,
				Domain: c.Domain,
			}
			if c.CustomAlphabet != "" {
				cfg.Alphabet = c.CustomAlphabet
			} else {
				cfg.Alphabet = c.Alphabet
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
			codec, err := dealcode.NewCycling(cfg)
			if err == nil {
				t.Fatalf("NewCycling(%s) = %v, want error", c.Name, codec)
			}
			if !errors.Is(err, dealcode.ErrConfig) {
				t.Fatalf("NewCycling(%s) error %v does not wrap ErrConfig", c.Name, err)
			}
		})
	}
}

// A full cycle issues each of the Capacity() possible strings exactly once,
// and consecutive cycles refill the same space in different orders.
func TestCyclingFullCycleIsAPermutationAndCyclesDiffer(t *testing.T) {
	codec, err := dealcode.NewCycling(dealcode.CyclingConfig{
		KeyString: "k", Alphabet: "dec", Length: 2, // capacity 100
	})
	if err != nil {
		t.Fatal(err)
	}
	if codec.Capacity() != 100 {
		t.Fatalf("Capacity() = %d, want 100", codec.Capacity())
	}
	var cycles [3][]string
	for e := int64(0); e < 3; e++ {
		codes := make([]string, 100)
		seen := make(map[string]bool, 100)
		for v := int64(0); v < 100; v++ {
			n := e*100 + v
			code, err := codec.Encode(n)
			if err != nil {
				t.Fatalf("Encode(%d): %v", n, err)
			}
			if len(code) != 2 {
				t.Fatalf("Encode(%d) = %q, want 2 characters", n, code)
			}
			if seen[code] {
				t.Fatalf("cycle %d: code %q repeated within the cycle", e, code)
			}
			seen[code] = true
			codes[v] = code
			got, err := codec.Decode(code, e)
			if err != nil {
				t.Fatalf("Decode(%q, %d): %v", code, e, err)
			}
			if got != n {
				t.Fatalf("Decode(%q, %d) = %d, want %d", code, e, got, n)
			}
		}
		cycles[e] = codes
	}
	// Same space every cycle (each is a permutation of all 100 strings),
	// refilled in a different order.
	for e := 1; e < 3; e++ {
		samePos := true
		for v := 0; v < 100; v++ {
			if cycles[e][v] != cycles[0][v] {
				samePos = false
				break
			}
		}
		if samePos {
			t.Errorf("cycle %d issues codes in the same order as cycle 0", e)
		}
	}
}

// Decoding under the wrong cycle succeeds but yields a different counter —
// the documented ambiguity: the cycle is context the application must keep.
func TestCyclingWrongCycleGivesADifferentCounter(t *testing.T) {
	codec, err := dealcode.NewCycling(dealcode.CyclingConfig{
		KeyString: "k", Alphabet: "crockford", Length: 6,
	})
	if err != nil {
		t.Fatal(err)
	}
	code, err := codec.Encode(7)
	if err != nil {
		t.Fatal(err)
	}
	if n, err := codec.Decode(code, 0); err != nil || n != 7 {
		t.Fatalf("Decode(%q, 0) = %d, %v, want 7", code, n, err)
	}
	n, err := codec.Decode(code, 1)
	if err != nil {
		t.Fatalf("Decode(%q, 1): %v", code, err)
	}
	if n == 7 {
		t.Fatalf("Decode(%q, 1) = 7; wrong cycle must map to a different counter", code)
	}
}

// The counter space's top value 2^63 - 1 must round-trip in the final
// partial cycle. (Encode(2^63) itself is unrepresentable: int64 tops out at
// 2^63 - 1, so the type system covers the upper bound.)
func TestCyclingFinalPartialCycleBoundary(t *testing.T) {
	codec, err := dealcode.NewCycling(dealcode.CyclingConfig{
		KeyString: "k", Alphabet: "dec", Length: 2,
	})
	if err != nil {
		t.Fatal(err)
	}
	top := int64(math.MaxInt64) // 2^63 - 1
	code, err := codec.Encode(top)
	if err != nil {
		t.Fatalf("Encode(%d): %v", top, err)
	}
	cycle, err := codec.CycleOf(top)
	if err != nil {
		t.Fatal(err)
	}
	if cycle != codec.MaxCycle() {
		t.Fatalf("CycleOf(%d) = %d, want MaxCycle() = %d", top, cycle, codec.MaxCycle())
	}
	got, err := codec.Decode(code, cycle)
	if err != nil {
		t.Fatalf("Decode(%q, %d): %v", code, cycle, err)
	}
	if got != top {
		t.Fatalf("Decode(%q, %d) = %d, want %d", code, cycle, got, top)
	}
	// In the final partial cycle some codes decrypt to counters >= 2^63;
	// each must be rejected as never issued. There are 100 - 8 = 92 such
	// codes in this configuration, so scanning all 100 finds them.
	rejected := 0
	for v := 0; v < 100; v++ {
		probe := string([]byte{'0' + byte(v/10), '0' + byte(v%10)})
		if _, err := codec.Decode(probe, cycle); errors.Is(err, dealcode.ErrInvalidCode) {
			rejected++
		}
	}
	if want := 100 - int(uint64(math.MaxInt64)%codec.Capacity()+1); rejected != want {
		t.Fatalf("final partial cycle rejected %d codes, want %d", rejected, want)
	}
}

// The boundary configuration radix^Length == 2^63 (octal, length 21) is
// legal: capacity is exactly 2^63 (uint64), max cycle is 0, and every
// non-negative int64 counter lives in cycle zero.
func TestCyclingCapacityExactly2Pow63(t *testing.T) {
	codec, err := dealcode.NewCycling(dealcode.CyclingConfig{
		KeyString: "k", Alphabet: "01234567", Length: 21,
	})
	if err != nil {
		t.Fatal(err)
	}
	if codec.Capacity() != 1<<63 {
		t.Fatalf("Capacity() = %d, want 2^63", codec.Capacity())
	}
	if codec.MaxCycle() != 0 {
		t.Fatalf("MaxCycle() = %d, want 0", codec.MaxCycle())
	}
	for _, n := range []int64{0, 1, math.MaxInt64} {
		code, err := codec.Encode(n)
		if err != nil {
			t.Fatalf("Encode(%d): %v", n, err)
		}
		cycle, err := codec.CycleOf(n)
		if err != nil || cycle != 0 {
			t.Fatalf("CycleOf(%d) = %d, %v, want 0", n, cycle, err)
		}
		got, err := codec.Decode(code, 0)
		if err != nil || got != n {
			t.Fatalf("Decode(%q, 0) = %d, %v, want %d", code, got, err, n)
		}
	}
	if _, err := codec.Decode("000000000000000000000", 1); !errors.Is(err, dealcode.ErrRange) {
		t.Fatalf("Decode(_, 1) error %v does not wrap ErrRange", err)
	}
}

// Cycling-mode construction applies the same guards as the plain codec.
func TestCyclingConfigGuards(t *testing.T) {
	cases := []struct {
		name string
		cfg  dealcode.CyclingConfig
	}{
		{"both keys set", dealcode.CyclingConfig{Key: []byte{1}, KeyString: "x", Alphabet: "hex", Length: 6}},
		{"no key", dealcode.CyclingConfig{Alphabet: "hex", Length: 6}},
		{"preset name as key", dealcode.CyclingConfig{KeyString: "crockford", Alphabet: "hex", Length: 6}},
		{"preset lookalike alphabet", dealcode.CyclingConfig{KeyString: "k", Alphabet: "HEX", Length: 6}},
		{"bad custom alphabet", dealcode.CyclingConfig{KeyString: "k", Alphabet: "aa", Length: 6}},
		{"length too small", dealcode.CyclingConfig{KeyString: "k", Alphabet: "hex", Length: 1}},
		{"length too large", dealcode.CyclingConfig{KeyString: "k", Alphabet: "hex", Length: 129}},
		{"negative length", dealcode.CyclingConfig{KeyString: "k", Alphabet: "hex", Length: -3}},
		{"space under 100", dealcode.CyclingConfig{KeyString: "k", Alphabet: "abcdefghi", Length: 2}},
		{"capacity over 2^63", dealcode.CyclingConfig{KeyString: "k", Alphabet: "hex", Length: 16}},
		{"domain over 255 bytes", dealcode.CyclingConfig{KeyString: "k", Alphabet: "hex", Length: 6, Domain: string(make([]byte, 256))}},
	}
	for _, tc := range cases {
		tc := tc
		t.Run(tc.name, func(t *testing.T) {
			codec, err := dealcode.NewCycling(tc.cfg)
			if err == nil {
				t.Fatalf("NewCycling = %v, want error", codec)
			}
			if !errors.Is(err, dealcode.ErrConfig) {
				t.Fatalf("error %v does not wrap ErrConfig", err)
			}
		})
	}
}

// Zero-value Length and Alphabet default to 6 and "hex", mirroring Config.
func TestCyclingDefaults(t *testing.T) {
	codec, err := dealcode.NewCycling(dealcode.CyclingConfig{KeyString: "example-key"})
	if err != nil {
		t.Fatal(err)
	}
	if codec.Length() != 6 || codec.Alphabet() != "0123456789abcdef" {
		t.Fatalf("defaults: length %d alphabet %q", codec.Length(), codec.Alphabet())
	}
	if codec.Capacity() != 1<<24 {
		t.Fatalf("Capacity() = %d, want 16^6", codec.Capacity())
	}
	code, err := codec.Encode(42)
	if err != nil {
		t.Fatal(err)
	}
	if len(code) != 6 {
		t.Fatalf("Encode(42) = %q, want 6 characters", code)
	}
}
