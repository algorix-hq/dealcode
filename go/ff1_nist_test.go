package dealcode

import (
	"encoding/hex"
	"encoding/json"
	"os"
	"reflect"
	"strings"
	"testing"
)

// nistAlphabet maps character index to numeral value for the NIST sample
// vectors' plaintext/ciphertext strings.
const nistAlphabet = "0123456789abcdefghijklmnopqrstuvwxyz"

type nistVector struct {
	Sample     int    `json:"sample"`
	Cipher     string `json:"cipher"`
	KeyHex     string `json:"key_hex"`
	Radix      int    `json:"radix"`
	TweakHex   string `json:"tweak_hex"`
	Plaintext  string `json:"plaintext"`
	Ciphertext string `json:"ciphertext"`
}

func nistNumerals(t *testing.T, s string) []int {
	t.Helper()
	out := make([]int, len(s))
	for i := 0; i < len(s); i++ {
		idx := strings.IndexByte(nistAlphabet, s[i])
		if idx < 0 {
			t.Fatalf("character %q not in NIST alphabet", s[i])
		}
		out[i] = idx
	}
	return out
}

func TestFF1NISTVectors(t *testing.T) {
	raw, err := os.ReadFile("../testvectors/ff1_nist.json")
	if err != nil {
		t.Fatalf("read vectors: %v", err)
	}
	var file struct {
		Vectors []nistVector `json:"vectors"`
	}
	if err := json.Unmarshal(raw, &file); err != nil {
		t.Fatalf("parse vectors: %v", err)
	}
	if len(file.Vectors) != 9 {
		t.Fatalf("expected 9 NIST vectors, got %d", len(file.Vectors))
	}

	for _, vec := range file.Vectors {
		vec := vec
		t.Run(vec.Cipher, func(t *testing.T) {
			key, err := hex.DecodeString(vec.KeyHex)
			if err != nil {
				t.Fatalf("key_hex: %v", err)
			}
			tweak, err := hex.DecodeString(vec.TweakHex)
			if err != nil {
				t.Fatalf("tweak_hex: %v", err)
			}
			f, err := newFF1(key, vec.Radix)
			if err != nil {
				t.Fatalf("newFF1: %v", err)
			}
			pt := nistNumerals(t, vec.Plaintext)
			ct := nistNumerals(t, vec.Ciphertext)
			params, err := f.params(tweak, len(pt))
			if err != nil {
				t.Fatalf("params: %v", err)
			}

			if got := f.encrypt(params, pt); !reflect.DeepEqual(got, ct) {
				t.Errorf("sample %d encrypt: got %v, want %v", vec.Sample, got, ct)
			}
			if got := f.decrypt(params, ct); !reflect.DeepEqual(got, pt) {
				t.Errorf("sample %d decrypt: got %v, want %v", vec.Sample, got, pt)
			}
		})
	}
}
