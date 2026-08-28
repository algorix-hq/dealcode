package dealcode

import (
	"errors"
	"strings"
	"testing"
	"time"
)

// Regression tests for the QA round-2 findings: absurd lengths must fail in
// O(1), and string inputs with U+0000 or invalid UTF-8 must be rejected
// rather than silently re-encoded (SPEC §2, §2.1).
func TestConfigGuards(t *testing.T) {
	reject := func(cfg Config, label string) {
		t.Helper()
		if _, err := New(cfg); !errors.Is(err, ErrConfig) {
			t.Fatalf("%s: expected ErrConfig, got %v", label, err)
		}
	}

	start := time.Now()
	reject(Config{KeyString: "k", MaxLength: 1 << 30}, "huge MaxLength")
	reject(Config{KeyString: "k", MinLength: 1 << 30}, "huge MinLength")
	if elapsed := time.Since(start); elapsed > 100*time.Millisecond {
		t.Fatalf("absurd lengths took %v; must be rejected in O(1)", elapsed)
	}

	reject(Config{KeyString: "k", Domain: "a\x00b"}, "NUL in domain")
	reject(Config{KeyString: "a\x00b"}, "NUL in key string")
	reject(Config{KeyString: "k", Domain: "\xff\xfe"}, "invalid UTF-8 domain")
	reject(Config{KeyString: "\xff\xfe"}, "invalid UTF-8 key string")

	// Legitimate Unicode still works.
	if _, err := New(Config{KeyString: "k", Domain: "한국어-✅"}); err != nil {
		t.Fatalf("legit unicode domain rejected: %v", err)
	}
}

// A custom alphabet that ASCII-case-insensitively equals a preset name is a
// misspelled preset, not a radix-3 codec over the letters of the name
// (SPEC §3.2).
func TestCustomAlphabetMatchingPresetNameRejected(t *testing.T) {
	for _, alpha := range []string{
		"DEC", "HEX", "Hex", "hEx", "BASE32", "Crockford", "CROCKFORD",
		"Base36", "BASE58", "Base62", "BASE64URL", "Base64Url",
	} {
		_, err := New(Config{KeyString: "k", Alphabet: alpha})
		if !errors.Is(err, ErrConfig) {
			t.Errorf("Alphabet %q: expected ErrConfig, got %v", alpha, err)
		}
	}

	_, err := New(Config{KeyString: "k", Alphabet: "HEX"})
	want := `custom alphabet "HEX" matches the preset name "hex" — pass "hex" for the preset, or a genuinely custom alphabet`
	if err == nil || !strings.Contains(err.Error(), want) {
		t.Errorf("Alphabet \"HEX\" error = %v, want message containing %q", err, want)
	}

	// Exact preset names keep resolving as presets.
	for name := range presets {
		codec, err := New(Config{KeyString: "k", Alphabet: name})
		if err != nil {
			t.Errorf("preset %q rejected: %v", name, err)
			continue
		}
		if codec.alpha.name != name {
			t.Errorf("preset %q resolved to %q", name, codec.alpha.name)
		}
	}

	// Genuinely custom alphabets are untouched, including ones containing
	// uppercase letters that do not spell a preset name.
	for _, alpha := range []string{"BCDFGHJKLMNPQRSTVWXZ", "0123456789ab", "HEX!"} {
		if _, err := New(Config{KeyString: "k", Alphabet: alpha}); err != nil {
			t.Errorf("custom alphabet %q rejected: %v", alpha, err)
		}
	}
}

// A string key that ASCII-case-insensitively equals a preset alphabet name is
// almost certainly a swapped argument (SPEC §2.1). Bytes keys are unaffected.
func TestStringKeyMatchingPresetNameRejected(t *testing.T) {
	for _, key := range []string{
		"dec", "hex", "HEX", "base32", "crockford", "Crockford",
		"base36", "base58", "base62", "base64url", "BASE64URL",
	} {
		_, err := New(Config{KeyString: key})
		if !errors.Is(err, ErrConfig) {
			t.Errorf("KeyString %q: expected ErrConfig, got %v", key, err)
		}
	}

	_, err := New(Config{KeyString: "crockford"})
	want := `string key "crockford" is a preset alphabet name — did you swap the key and alphabet arguments?`
	if err == nil || !strings.Contains(err.Error(), want) {
		t.Errorf("KeyString \"crockford\" error = %v, want message containing %q", err, want)
	}

	// Bytes keys with the same content are fine.
	if _, err := New(Config{Key: []byte("crockford")}); err != nil {
		t.Errorf("bytes key \"crockford\" rejected: %v", err)
	}

	// Ordinary string keys still work.
	for _, key := range []string{"hexadecimal", "hex ", "my-secret", "base65"} {
		if _, err := New(Config{KeyString: key}); err != nil {
			t.Errorf("KeyString %q rejected: %v", key, err)
		}
	}
}
