package dealcode

// Fixed-length cycling mode (SPEC.md §11, tweak namespace "dealcode/v1c/").

import (
	"fmt"
	"math/big"
	"strconv"
	"strings"
	"unicode/utf8"
)

// cycleTweakPrefix starts every cycling-mode FF1 tweak:
// "dealcode/v1c/" + decimal(cycle) + "/" + domain. The byte at offset 11
// ('c' vs '/') keeps the namespace disjoint from plain-v1 tweaks for every
// possible domain and cycle.
const cycleTweakPrefix = "dealcode/v1c/"

// CyclingConfig describes a fixed-length cycling codec (SPEC.md §11).
//
// Exactly one of Key and KeyString must be set; the key rules are identical
// to Config's. As with Config, the entire configuration — key material,
// alphabet, length, and domain — must never change once codes have been
// issued.
type CyclingConfig struct {
	// Key is binary key material, with exactly the rules of Config.Key.
	Key []byte

	// KeyString is string key material, with exactly the rules of
	// Config.KeyString.
	KeyString string

	// Alphabet is a preset name or custom alphabet string, with exactly the
	// rules of Config.Alphabet. Empty defaults to "hex".
	Alphabet string

	// Length is the fixed code length L: every code is exactly L characters
	// in every cycle. Zero defaults to 6. It must be in [2, 128] with
	// 100 <= radix^L <= 2^63 (exactly 2^63 is allowed); for larger fixed
	// spaces use Codec with MinLength == MaxLength instead.
	Length int

	// Domain is an application-chosen namespace label, with exactly the
	// rules of Config.Domain.
	Domain string
}

// CycleCodec is a fixed-length cycling codec (dealcode mode v1c): codes are
// always exactly Length() characters, and the counter space is spent in
// cycles of Capacity() codes each. Counter n belongs to cycle n / Capacity()
// with in-cycle value n % Capacity(); every cycle is a different permutation
// of the same code space (a different FF1 tweak), so when the space is
// exhausted it refills in a new order instead of growing.
//
// Codes REPEAT across cycles by design (pigeonhole: the same space is being
// refilled). Keep at most one cycle's codes live per uniqueness scope — a
// global UNIQUE(code) index spanning cycles WILL fire; scope it as
// UNIQUE(cycle, code) — and persist which cycle each live code belongs to:
// Decode needs it, and the library cannot recover the cycle from the code
// string.
//
// A CycleCodec is immutable after NewCycling and safe for concurrent use by
// multiple goroutines without external locking.
type CycleCodec struct {
	alpha    alphabet
	radix    int
	length   int
	domain   string
	f        *ff1
	index    [256]int16 // byte -> numeral value, -1 if not in the alphabet
	capacity uint64     // radix^length; may be exactly 2^63 (1 << 63)
	maxCycle int64      // (2^63 - 1) / capacity
}

// NewCycling validates cfg and builds a CycleCodec. All configuration
// violations from SPEC.md §11.1 are reported as errors wrapping ErrConfig.
func NewCycling(cfg CyclingConfig) (*CycleCodec, error) {
	aesKey, err := resolveKey(Config{Key: cfg.Key, KeyString: cfg.KeyString})
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

	length := cfg.Length
	if length == 0 {
		length = 6
	}
	// Bound the length BEFORE any big.Int exponentiation (SPEC §11.1):
	// unguarded Exp on attacker-sized lengths would allocate without limit.
	if length < 2 || length > 128 {
		return nil, fmt.Errorf("%w: Length must be in [2, 128], got %d", ErrConfig, cfg.Length)
	}
	space := new(big.Int).Exp(big.NewInt(int64(radix)), big.NewInt(int64(length)), nil)
	if space.Cmp(big.NewInt(100)) < 0 {
		return nil, fmt.Errorf("%w: radix^Length must be at least 100 (FF1 minimum domain), got %s", ErrConfig, space)
	}
	// The per-cycle capacity must itself fit the counter space, so that a
	// cycle can complete; exactly 2^63 is allowed (SPEC §11.1). The check
	// uses big.Int because 2^63 overflows int64.
	if space.Cmp(counterBoundBig) > 0 {
		return nil, fmt.Errorf("%w: radix^Length must not exceed 2^63 in cycling mode — a cycle must be completable; use Codec with MinLength == MaxLength for larger fixed spaces", ErrConfig)
	}

	if !utf8.ValidString(cfg.Domain) {
		return nil, fmt.Errorf("%w: Domain must be valid UTF-8", ErrConfig)
	}
	if strings.IndexByte(cfg.Domain, 0) >= 0 {
		return nil, fmt.Errorf("%w: Domain must not contain U+0000", ErrConfig)
	}
	if len(cfg.Domain) > 255 {
		return nil, fmt.Errorf("%w: Domain must be at most 255 UTF-8 bytes, got %d", ErrConfig, len(cfg.Domain))
	}

	f, err := newFF1(aesKey, radix)
	if err != nil {
		return nil, fmt.Errorf("%w: %v", ErrConfig, err)
	}

	c := &CycleCodec{
		alpha:  alpha,
		radix:  radix,
		length: length,
		domain: cfg.Domain,
		f:      f,
	}
	for i := range c.index {
		c.index[i] = -1
	}
	for i := 0; i < len(alpha.chars); i++ {
		c.index[alpha.chars[i]] = int16(i)
	}

	// capacity = radix^length <= 2^63 fits uint64 exactly, including the
	// boundary case radix^length == 2^63 (e.g. radix 8, length 21), which
	// would overflow int64. In that case maxCycle is 0 and every counter
	// belongs to cycle zero.
	c.capacity = space.Uint64()
	c.maxCycle = int64(uint64(1<<63-1) / c.capacity)

	// Fail fast on any FF1 parameter problem (all inputs are validated
	// above, so this cannot fail; keep the check for defence in depth).
	if _, err := f.params(c.tweakFor(0), length); err != nil {
		return nil, fmt.Errorf("%w: %v", ErrConfig, err)
	}
	return c, nil
}

// Alphabet returns the codec's alphabet characters in numeral order (the
// character at index i represents numeral value i).
func (c *CycleCodec) Alphabet() string { return c.alpha.chars }

// Radix returns the number of characters in the alphabet.
func (c *CycleCodec) Radix() int { return c.radix }

// Length returns the fixed code length: every code is exactly this many
// characters, in every cycle.
func (c *CycleCodec) Length() int { return c.length }

// Domain returns the codec's namespace label.
func (c *CycleCodec) Domain() string { return c.domain }

// Capacity returns the number of codes per cycle: radix^Length(). It is a
// uint64 because the boundary configuration radix^Length == 2^63 is legal
// and 2^63 overflows int64.
func (c *CycleCodec) Capacity() uint64 { return c.capacity }

// MaxCycle returns the largest usable cycle number:
// (2^63 - 1) / Capacity(). Decode accepts cycles in [0, MaxCycle()].
func (c *CycleCodec) MaxCycle() int64 { return c.maxCycle }

// String describes the codec's public configuration. Key material never
// appears in the output.
func (c *CycleCodec) String() string {
	name := c.alpha.name
	if name == "" {
		name = fmt.Sprintf("custom(%d)", c.radix)
	}
	return fmt.Sprintf("dealcode.CycleCodec(alphabet=%s, length=%d, domain=%q)",
		name, c.length, c.domain)
}

// CycleOf returns the cycle that counter n belongs to: n / Capacity(). It
// returns an error wrapping ErrRange when n is negative (every non-negative
// int64 is a valid counter in cycling mode).
func (c *CycleCodec) CycleOf(n int64) (int64, error) {
	if n < 0 {
		return 0, fmt.Errorf("%w: counter %d outside [0, 2^63)", ErrRange, n)
	}
	return int64(uint64(n) / c.capacity), nil
}

// Encode maps counter n to its fixed-length code (SPEC.md §11.2). The code
// belongs to cycle n / Capacity() — the caller must record that cycle (or
// the currently active cycle) to decode later. Encode returns an error
// wrapping ErrRange when n is negative; every non-negative int64 is a valid
// counter.
//
// Codes repeat across cycles: Encode(n) and Encode(n + Capacity()) can
// return the same string for two different counters. See CycleCodec.
func (c *CycleCodec) Encode(n int64) (string, error) {
	if n < 0 {
		return "", fmt.Errorf("%w: counter %d outside [0, 2^63)", ErrRange, n)
	}
	// uint64 arithmetic covers the capacity == 2^63 boundary configuration:
	// there cycle is always 0 and v == n.
	cycle := uint64(n) / c.capacity
	v := uint64(n) % c.capacity
	numerals := make([]int, c.length)
	r := uint64(c.radix)
	for i := c.length - 1; i >= 0; i-- {
		numerals[i] = int(v % r)
		v /= r
	}
	cipher := c.f.encrypt(c.paramsFor(cycle), numerals)
	buf := make([]byte, c.length)
	for i, x := range cipher {
		buf[i] = c.alpha.chars[x]
	}
	return string(buf), nil
}

// Decode maps a code issued in the given cycle back to its counter (SPEC.md
// §11.2). The cycle is required: the same string recurs in every cycle,
// mapping to a different counter each time, so a code alone is ambiguous by
// design.
//
// A cycle outside [0, MaxCycle()] yields an error wrapping ErrRange. Any
// string this codec could never have issued in that cycle — wrong length,
// characters outside the alphabet (after the alphabet's normalization), or
// a counter at or beyond 2^63 (possible only in the final partial cycle) —
// yields an error wrapping ErrInvalidCode.
//
// Decode success only proves the code is consistent with the key and cycle;
// the application still decides whether counter n actually exists.
func (c *CycleCodec) Decode(code string, cycle int64) (int64, error) {
	if cycle < 0 || cycle > c.maxCycle {
		return 0, fmt.Errorf("%w: cycle %d outside [0, %d]", ErrRange, cycle, c.maxCycle)
	}
	// Length gate before normalization, exactly as in Codec.Decode: the
	// gate counts runes; everything after stays byte-based because any
	// multi-byte input contains a byte >= 0x80, which the charset check
	// rejects.
	if n := utf8.RuneCountInString(code); n != c.length {
		return 0, fmt.Errorf("%w: code length %d != %d (fixed-length mode)", ErrInvalidCode, n, c.length)
	}
	normalized := c.alpha.normalize(code)
	numerals := make([]int, c.length)
	for i, ch := range normalized {
		idx := c.index[ch]
		if idx < 0 {
			return 0, fmt.Errorf("%w: character %q not in alphabet", ErrInvalidCode, rune(ch))
		}
		numerals[i] = int(idx)
	}
	plain := c.f.decrypt(c.paramsFor(uint64(cycle)), numerals)
	// v < capacity <= 2^63 fits uint64, so no big-integer arithmetic is
	// needed; the stage range check of SPEC §7 reduces to v < capacity,
	// which always holds for a full-width numeral string.
	var v uint64
	r := uint64(c.radix)
	for _, x := range plain {
		v = v*r + uint64(x)
	}
	// n = cycle*capacity + v <= (2^63 - 1) + (2^63 - 1) fits uint64.
	n := uint64(cycle)*c.capacity + v
	if n >= 1<<63 { // only reachable in the final partial cycle
		return 0, fmt.Errorf("%w: code was not issued in this cycle", ErrInvalidCode)
	}
	return int64(n), nil
}

// tweakFor renders the cycling-mode FF1 tweak for one cycle:
// "dealcode/v1c/" + decimal(cycle) + "/" + domain (SPEC.md §11.2). The
// cycle is rendered base-10 with no leading zeros ("0" for cycle zero);
// with domain <= 255 bytes and cycle <= 2^63-1 the tweak is at most 288
// bytes.
func (c *CycleCodec) tweakFor(cycle uint64) []byte {
	buf := make([]byte, 0, len(cycleTweakPrefix)+20+1+len(c.domain))
	buf = append(buf, cycleTweakPrefix...)
	buf = strconv.AppendUint(buf, cycle, 10)
	buf = append(buf, '/')
	return append(buf, c.domain...)
}

// paramsFor computes the FF1 parameters for one cycle's tweak. It cannot
// fail: NewCycling validated the length and domain size.
func (c *CycleCodec) paramsFor(cycle uint64) *ff1Params {
	params, err := c.f.params(c.tweakFor(cycle), c.length)
	if err != nil {
		// Unreachable: length >= 2 and radix^length >= 100 are checked at
		// construction, and params has no other failure mode.
		panic(fmt.Sprintf("dealcode: internal error: %v", err))
	}
	return params
}
