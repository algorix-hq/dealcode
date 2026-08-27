package dealcode

import (
	"crypto/sha256"
	"fmt"
	"math/big"
	"unicode/utf8"
)

const tweakPrefix = "dealcode/v1/" // FF1 tweak = tweakPrefix + domain (UTF-8)

var (
	kdfPrefix       = []byte("dealcode/v1/kdf") // 15-byte KDF domain-separation prefix
	counterBoundBig = new(big.Int).Lsh(big.NewInt(1), 63)
	codespaceBound  = new(big.Int).Lsh(big.NewInt(1), 128)
)

// Config describes a dealcode codec (SPEC.md §2).
//
// Exactly one of Key and KeyString must be set. For a given code namespace
// (one counter sequence) the entire configuration — key material, alphabet,
// lengths, and domain — must never change once codes have been issued;
// changing any of it creates a second, unrelated permutation whose outputs
// may collide with already-issued codes.
type Config struct {
	// Key is binary key material. Bytes of length exactly 16, 24, or 32 are
	// used directly as the AES key; any other non-zero length is expanded to
	// an AES-256 key via SHA-256("dealcode/v1/kdf" || Key).
	Key []byte

	// KeyString is string key material (a passphrase, hex blob, base64 blob —
	// anything). It is always expanded, regardless of length or content, via
	// SHA-256("dealcode/v1/kdf" || UTF-8 bytes); a hex-looking string is not
	// auto-decoded. A passphrase key is exactly as strong as the passphrase;
	// prefer >=128-bit random material (e.g. `openssl rand -hex 32`).
	KeyString string

	// Alphabet is a preset name — "dec", "hex", "base32", "crockford",
	// "base36", "base58", "base62", "base64url" (see Preset) — or a custom
	// alphabet string of 2 to 94 distinct printable ASCII characters
	// (0x21-0x7E). Preset names win on conflict. Empty defaults to "hex".
	Alphabet string

	// MinLength is the length codes start at. Zero defaults to 6. It must be
	// at least 2, with radix^MinLength >= 100 (the FF1 structural minimum).
	MinLength int

	// MaxLength is the length codes may grow to. Zero defaults to the largest
	// L with radix^L <= 2^63-1 (hex: 15, dec: 18, base32/crockford: 12,
	// base58/base62/base64url: 10, ...). It must satisfy
	// MinLength <= MaxLength and radix^MaxLength <= 2^128. Set
	// MinLength == MaxLength for fixed-length codes.
	MaxLength int

	// Domain is an application-chosen namespace label (e.g. "orders",
	// "coupons"), bound into the FF1 tweak: two codecs with the same key but
	// different domains produce unrelated permutations. It must be valid
	// UTF-8 of at most 255 bytes. Empty is a valid (default) domain.
	Domain string
}

// Codec is a bijective counter <-> code mapping (dealcode format version 1).
//
// A Codec is immutable after New and safe for concurrent use by multiple
// goroutines without external locking. It is cheap to keep around: create one
// per code namespace at startup and reuse it.
type Codec struct {
	alpha     alphabet
	radix     int
	minLength int
	maxLength int
	domain    string
	tweak     []byte
	f         *ff1
	index     [256]int16 // byte -> numeral value, -1 if not in the alphabet
	pow64     []uint64   // pow64[d] = min(radix^d, 2^63), d in [0, maxLength]
	capacity  uint64     // min(radix^maxLength, 2^63)
	stages    []stage    // per code length d, indexed by d - minLength
}

// stage holds the precomputed constants for one code length d.
type stage struct {
	params  *ff1Params
	baseBig *big.Int // base(d): 0 for d == minLength, else radix^(d-1)
	sizeBig *big.Int // capacity(d) = radix^d - base(d)
}

// resolveKey applies the key-material rules of SPEC.md §2.1 and returns the
// AES key.
func resolveKey(cfg Config) ([]byte, error) {
	hasBytes := cfg.Key != nil
	hasString := cfg.KeyString != ""
	switch {
	case hasBytes && hasString:
		return nil, fmt.Errorf("%w: exactly one of Key and KeyString may be set", ErrConfig)
	case hasString:
		sum := sha256.Sum256(append(append([]byte{}, kdfPrefix...), cfg.KeyString...))
		return sum[:], nil
	case !hasBytes || len(cfg.Key) == 0:
		return nil, fmt.Errorf("%w: key must not be empty", ErrConfig)
	}
	switch len(cfg.Key) {
	case 16, 24, 32:
		return append([]byte(nil), cfg.Key...), nil
	}
	sum := sha256.Sum256(append(append([]byte{}, kdfPrefix...), cfg.Key...))
	return sum[:], nil
}

// New validates cfg and builds a Codec. All configuration violations from
// SPEC.md §2 are reported as errors wrapping ErrConfig.
func New(cfg Config) (*Codec, error) {
	aesKey, err := resolveKey(cfg)
	if err != nil {
		return nil, err
	}

	alphaSpec := cfg.Alphabet
	if alphaSpec == "" {
		alphaSpec = "hex"
	}
	alpha, err := resolveAlphabet(alphaSpec)
	if err != nil {
		return nil, fmt.Errorf("%w: %v", ErrConfig, err)
	}
	radix := len(alpha.chars)
	radixBig := big.NewInt(int64(radix))

	minLength := cfg.MinLength
	if minLength == 0 {
		minLength = 6
	}
	if minLength < 2 {
		return nil, fmt.Errorf("%w: MinLength must be >= 2, got %d", ErrConfig, cfg.MinLength)
	}
	minSpace := new(big.Int).Exp(radixBig, big.NewInt(int64(minLength)), nil)
	if minSpace.Cmp(big.NewInt(100)) < 0 {
		return nil, fmt.Errorf("%w: radix^MinLength must be at least 100 (FF1 minimum domain), got %s", ErrConfig, minSpace)
	}

	maxLength := cfg.MaxLength
	if maxLength == 0 {
		// Largest L with radix^L <= 2^63 - 1, but never below MinLength.
		maxLength = minLength
		space := new(big.Int).Set(minSpace)
		for space.Mul(space, radixBig); space.Cmp(counterBoundBig) < 0; space.Mul(space, radixBig) {
			maxLength++
		}
	}
	if maxLength < minLength {
		return nil, fmt.Errorf("%w: MaxLength must be >= MinLength (%d), got %d", ErrConfig, minLength, maxLength)
	}
	maxSpace := new(big.Int).Exp(radixBig, big.NewInt(int64(maxLength)), nil)
	if maxSpace.Cmp(codespaceBound) > 0 {
		return nil, fmt.Errorf("%w: radix^MaxLength must not exceed 2^128", ErrConfig)
	}

	if !utf8.ValidString(cfg.Domain) {
		return nil, fmt.Errorf("%w: Domain must be valid UTF-8", ErrConfig)
	}
	if len(cfg.Domain) > 255 {
		return nil, fmt.Errorf("%w: Domain must be at most 255 UTF-8 bytes, got %d", ErrConfig, len(cfg.Domain))
	}

	f, err := newFF1(aesKey, radix)
	if err != nil {
		return nil, fmt.Errorf("%w: %v", ErrConfig, err)
	}

	c := &Codec{
		alpha:     alpha,
		radix:     radix,
		minLength: minLength,
		maxLength: maxLength,
		domain:    cfg.Domain,
		tweak:     []byte(tweakPrefix + cfg.Domain),
		f:         f,
	}

	for i := range c.index {
		c.index[i] = -1
	}
	for i := 0; i < len(alpha.chars); i++ {
		c.index[alpha.chars[i]] = int16(i)
	}

	// powers[d] = radix^d (exact); pow64[d] = min(radix^d, 2^63).
	powers := make([]*big.Int, maxLength+1)
	c.pow64 = make([]uint64, maxLength+1)
	p := big.NewInt(1)
	for d := 0; d <= maxLength; d++ {
		powers[d] = new(big.Int).Set(p)
		if p.Cmp(counterBoundBig) >= 0 {
			c.pow64[d] = 1 << 63
		} else {
			c.pow64[d] = p.Uint64()
		}
		p.Mul(p, radixBig)
	}
	c.capacity = c.pow64[maxLength]

	c.stages = make([]stage, maxLength-minLength+1)
	for d := minLength; d <= maxLength; d++ {
		params, err := f.params(c.tweak, d)
		if err != nil {
			return nil, fmt.Errorf("%w: %v", ErrConfig, err)
		}
		baseBig := big.NewInt(0)
		if d > minLength {
			baseBig = powers[d-1]
		}
		c.stages[d-minLength] = stage{
			params:  params,
			baseBig: baseBig,
			sizeBig: new(big.Int).Sub(powers[d], baseBig),
		}
	}
	return c, nil
}

// Alphabet returns the codec's alphabet characters in numeral order (the
// character at index i represents numeral value i).
func (c *Codec) Alphabet() string { return c.alpha.chars }

// Radix returns the number of characters in the alphabet.
func (c *Codec) Radix() int { return c.radix }

// MinLength returns the length codes start at.
func (c *Codec) MinLength() int { return c.minLength }

// MaxLength returns the length codes may grow to.
func (c *Codec) MaxLength() int { return c.maxLength }

// Domain returns the codec's namespace label.
func (c *Codec) Domain() string { return c.domain }

// Capacity returns the number of encodable counters:
// min(radix^MaxLength, 2^63). Encode accepts exactly [0, Capacity()).
func (c *Codec) Capacity() uint64 { return c.capacity }

// String describes the codec's public configuration. Key material never
// appears in the output.
func (c *Codec) String() string {
	name := c.alpha.name
	if name == "" {
		name = fmt.Sprintf("custom(%d)", c.radix)
	}
	return fmt.Sprintf("dealcode.Codec(alphabet=%s, min_length=%d, max_length=%d, domain=%q)",
		name, c.minLength, c.maxLength, c.domain)
}

// Encode maps counter n to its code (SPEC.md §5). The code's length depends
// only on n's stage: MinLength() characters until the counter reaches
// radix^MinLength, one more character per exhausted stage after that. Encode
// is O(1) in n and returns an error wrapping ErrRange when n is outside
// [0, Capacity()).
func (c *Codec) Encode(n int64) (string, error) {
	if n < 0 || uint64(n) >= c.capacity {
		return "", fmt.Errorf("%w: counter %d outside [0, %d)", ErrRange, n, c.capacity)
	}
	un := uint64(n)
	d := c.minLength
	for d < c.maxLength && un >= c.pow64[d] {
		d++
	}
	var base uint64
	if d > c.minLength {
		base = c.pow64[d-1]
	}
	// The stage value fits uint64 (it is below 2^63), so the numeral
	// conversion needs no big-integer arithmetic.
	v := un - base
	r := uint64(c.radix)
	numerals := make([]int, d)
	for i := d - 1; i >= 0; i-- {
		numerals[i] = int(v % r)
		v /= r
	}
	cipher := c.f.encrypt(c.stages[d-c.minLength].params, numerals)
	buf := make([]byte, d)
	for i, x := range cipher {
		buf[i] = c.alpha.chars[x]
	}
	return string(buf), nil
}

// Decode maps a code back to its counter (SPEC.md §7). The alphabet's
// normalization (e.g. hex is case-insensitive, crockford also folds O->0 and
// I/L->1) is applied first; custom alphabets require an exact match. Any
// string this codec could never have issued — wrong length, characters
// outside the alphabet, or a value outside the code's stage or the counter
// space — yields an error wrapping ErrInvalidCode.
//
// Decode success only proves the code is consistent with the key; the
// application still decides whether counter n actually exists.
func (c *Codec) Decode(code string) (int64, error) {
	normalized := c.alpha.normalize(code)
	d := len(normalized)
	if d < c.minLength || d > c.maxLength {
		return 0, fmt.Errorf("%w: code length %d outside [%d, %d]", ErrInvalidCode, d, c.minLength, c.maxLength)
	}
	numerals := make([]int, d)
	for i, ch := range normalized {
		idx := c.index[ch]
		if idx < 0 {
			return 0, fmt.Errorf("%w: character %q not in alphabet", ErrInvalidCode, rune(ch))
		}
		numerals[i] = int(idx)
	}
	st := &c.stages[d-c.minLength]
	plain := c.f.decrypt(st.params, numerals)
	var v, tmp big.Int
	c.f.num(plain, &v, &tmp)
	if d > c.minLength && v.Cmp(st.sizeBig) >= 0 {
		return 0, fmt.Errorf("%w: code was not issued by this codec", ErrInvalidCode)
	}
	n := tmp.Add(st.baseBig, &v)
	if n.Cmp(counterBoundBig) >= 0 {
		return 0, fmt.Errorf("%w: code was not issued by this codec", ErrInvalidCode)
	}
	return n.Int64(), nil
}
