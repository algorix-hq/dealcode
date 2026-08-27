package dealcode

import "fmt"

// normKind selects the decode-input normalization applied by a preset
// alphabet (SPEC.md §3.1). Normalization applies to Decode input only;
// Encode always emits the canonical characters.
type normKind int

const (
	normNone      normKind = iota // no normalization (custom, dec, base58, base62, base64url)
	normLower                     // ASCII-lowercase A-Z only (hex, base36)
	normUpper                     // ASCII-uppercase a-z only (base32)
	normCrockford                 // ASCII-uppercase, then O->0, I->1, L->1
)

// alphabet is a resolved alphabet: an ordered sequence of distinct characters
// where the character at index i represents numeral value i.
type alphabet struct {
	name  string // preset name, or "" for a custom alphabet
	chars string
	norm  normKind
}

// presets are the eight preset alphabets from SPEC.md §3.1.
var presets = map[string]alphabet{
	"dec":       {"dec", "0123456789", normNone},
	"hex":       {"hex", "0123456789abcdef", normLower},
	"base32":    {"base32", "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567", normUpper},
	"crockford": {"crockford", "0123456789ABCDEFGHJKMNPQRSTVWXYZ", normCrockford},
	"base36":    {"base36", "0123456789abcdefghijklmnopqrstuvwxyz", normLower},
	"base58":    {"base58", "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz", normNone},
	"base62":    {"base62", "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz", normNone},
	"base64url": {"base64url", "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_", normNone},
}

// Preset returns the character set of the named preset alphabet ("dec",
// "hex", "base32", "crockford", "base36", "base58", "base62", "base64url")
// and reports whether the name is a known preset. The character at index i
// represents numeral value i.
func Preset(name string) (chars string, ok bool) {
	a, ok := presets[name]
	return a.chars, ok
}

// resolveAlphabet resolves a preset name or validates a custom alphabet
// string (SPEC.md §3.2). Preset names win on conflict.
func resolveAlphabet(s string) (alphabet, error) {
	if a, ok := presets[s]; ok {
		return a, nil
	}
	if len(s) < 2 || len(s) > 94 {
		return alphabet{}, fmt.Errorf("custom alphabet must have 2 to 94 characters, got %d", len(s))
	}
	var seen [256]bool
	for i := 0; i < len(s); i++ {
		c := s[i]
		if c < 0x21 || c > 0x7e {
			return alphabet{}, fmt.Errorf("custom alphabet must be printable ASCII (0x21-0x7E), got byte 0x%02x", c)
		}
		if seen[c] {
			return alphabet{}, fmt.Errorf("custom alphabet characters must be distinct, %q repeats", c)
		}
		seen[c] = true
	}
	return alphabet{name: "", chars: s, norm: normNone}, nil
}

// normalize applies the alphabet's decode normalization to code and returns
// the normalized bytes. Only ASCII letters are mapped; every other byte is
// left untouched (and, if not in the alphabet, rejected later by Decode).
func (a alphabet) normalize(code string) []byte {
	b := []byte(code)
	switch a.norm {
	case normLower:
		for i, c := range b {
			if 'A' <= c && c <= 'Z' {
				b[i] = c + ('a' - 'A')
			}
		}
	case normUpper:
		for i, c := range b {
			if 'a' <= c && c <= 'z' {
				b[i] = c - ('a' - 'A')
			}
		}
	case normCrockford:
		for i, c := range b {
			if 'a' <= c && c <= 'z' {
				c = c - ('a' - 'A')
			}
			switch c {
			case 'O':
				c = '0'
			case 'I', 'L':
				c = '1'
			}
			b[i] = c
		}
	}
	return b
}
