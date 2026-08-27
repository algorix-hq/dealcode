package dealcode

import (
	"errors"
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
