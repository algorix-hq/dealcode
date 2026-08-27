package dealcode_test

import (
	"encoding/hex"
	"encoding/json"
	"errors"
	"os"
	"strconv"
	"testing"

	dealcode "github.com/algorix-hq/dealcode/go"
)

type v1Config struct {
	Name           string  `json:"name"`
	Alphabet       string  `json:"alphabet"`
	CustomAlphabet string  `json:"custom_alphabet"`
	KeyHex         *string `json:"key_hex"`
	KeyString      *string `json:"key_string"`
	MinLength      int     `json:"min_length"`
	MaxLength      int     `json:"max_length"`
	Domain         string  `json:"domain"`
	Vectors        []struct {
		N    string `json:"n"`
		Code string `json:"code"`
	} `json:"vectors"`
	InvalidCodes []string `json:"invalid_codes"`
	Normalize    []struct {
		Input string `json:"input"`
		N     string `json:"n"`
	} `json:"normalize"`
}

func loadV1Configs(t *testing.T) []v1Config {
	t.Helper()
	raw, err := os.ReadFile("../testvectors/v1.json")
	if err != nil {
		t.Fatalf("read vectors: %v", err)
	}
	var file struct {
		Spec    string     `json:"spec"`
		Configs []v1Config `json:"configs"`
	}
	if err := json.Unmarshal(raw, &file); err != nil {
		t.Fatalf("parse vectors: %v", err)
	}
	if file.Spec != "dealcode/v1" {
		t.Fatalf("unexpected spec %q", file.Spec)
	}
	if len(file.Configs) == 0 {
		t.Fatal("no configs in v1.json")
	}
	return file.Configs
}

func codecFor(t *testing.T, c v1Config) *dealcode.Codec {
	t.Helper()
	cfg := dealcode.Config{
		MinLength: c.MinLength,
		MaxLength: c.MaxLength,
		Domain:    c.Domain,
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
	codec, err := dealcode.New(cfg)
	if err != nil {
		t.Fatalf("New(%s): %v", c.Name, err)
	}
	return codec
}

func TestSpecVectors(t *testing.T) {
	for _, c := range loadV1Configs(t) {
		c := c
		t.Run(c.Name, func(t *testing.T) {
			codec := codecFor(t, c)

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
				got, err := codec.Decode(vec.Code)
				if err != nil {
					t.Errorf("Decode(%q): %v", vec.Code, err)
					continue
				}
				if got != n {
					t.Errorf("Decode(%q) = %d, want %d", vec.Code, got, n)
				}
			}

			for _, bad := range c.InvalidCodes {
				n, err := codec.Decode(bad)
				if err == nil {
					t.Errorf("Decode(%q) = %d, want error", bad, n)
					continue
				}
				if !errors.Is(err, dealcode.ErrInvalidCode) {
					t.Errorf("Decode(%q) error %v does not wrap ErrInvalidCode", bad, err)
				}
			}

			for _, norm := range c.Normalize {
				n, err := strconv.ParseInt(norm.N, 10, 64)
				if err != nil {
					t.Fatalf("counter %q: %v", norm.N, err)
				}
				got, err := codec.Decode(norm.Input)
				if err != nil {
					t.Errorf("Decode(%q): %v", norm.Input, err)
					continue
				}
				if got != n {
					t.Errorf("Decode(%q) = %d, want %d", norm.Input, got, n)
				}
			}
		})
	}
}
