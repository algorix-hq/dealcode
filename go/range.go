package dealcode

// Integer range mode (SPEC.md §12, tweak namespace "dealcode/v1r/").

import (
	"fmt"
	"strconv"
	"strings"
	"unicode/utf8"
)

// rangeTweakPrefix starts every range-mode FF1 tweak:
// "dealcode/v1r/" + decimal(low) + "/" + decimal(high) + "/" + domain. The
// byte at offset 11 ('r' vs '/' and 'c') keeps the namespace disjoint from
// plain-v1 and cycling-mode tweaks for every possible configuration.
const rangeTweakPrefix = "dealcode/v1r/"

// maxRangeRadix bounds the internal FF1 radix: numerals stay one byte in
// every FF1 core (SPEC §12.2).
const maxRangeRadix = 256

// RangeConfig describes an integer range codec (SPEC.md §12).
//
// Exactly one of Key and KeyString must be set; the key rules are identical
// to Config's. As with Config, the entire configuration — key material, low,
// high, and domain — must never change once codes have been issued.
type RangeConfig struct {
	// Key is binary key material, with exactly the rules of Config.Key.
	Key []byte

	// KeyString is string key material, with exactly the rules of
	// Config.KeyString.
	KeyString string

	// Low is the smallest code the range may contain. It must satisfy
	// 0 <= Low <= High.
	Low int64

	// High is the largest code the range may contain. The range must span at
	// least 100 values: High - Low + 1 >= 100 (the FF1 structural minimum).
	// The int64 type enforces the SPEC §12.1 upper bound High <= 2^63 - 1.
	High int64

	// Domain is an application-chosen namespace label, with exactly the
	// rules of Config.Domain.
	Domain string
}

// RangeCodec issues integer codes drawn without repetition from [Low(),
// High()] (dealcode mode v1r): counters 0 <= n < Capacity() map bijectively
// to integer codes in [Low(), Low() + Capacity() - 1] through a single FF1
// call — no loops, no cycle-walking. Capacity() is the largest FF1 domain
// (radix^m with radix <= 256) that fits in the range, so it can be slightly
// smaller than High() - Low() + 1; the uncovered top slice — the dead zone —
// is never issued and is rejected by Decode.
//
// Built for ranges like 100000-999999: every code is a 6-digit integer with
// no leading zero, safe to store in an integer column and to round-trip
// through any system that would strip a leading zero from a string code.
//
// A RangeCodec is immutable after NewRange and safe for concurrent use by
// multiple goroutines without external locking.
type RangeCodec struct {
	low      int64
	high     int64
	domain   string
	radix    int
	m        int    // numerals per FF1 message; capacity = radix^m
	capacity uint64 // largest radix^m <= high - low + 1; may be exactly 2^63
	f        *ff1
	params   *ff1Params // for the single fixed tweak of this codec
}

// NewRange validates cfg and builds a RangeCodec. All configuration
// violations from SPEC.md §12.1 are reported as errors wrapping ErrConfig.
func NewRange(cfg RangeConfig) (*RangeCodec, error) {
	aesKey, err := resolveKey(Config{Key: cfg.Key, KeyString: cfg.KeyString})
	if err != nil {
		return nil, err
	}

	// SPEC §12.1: 0 <= low <= high <= 2^63 - 1. The int64 type enforces the
	// upper bound; the lower bound and the ordering are checked here.
	if cfg.Low < 0 || cfg.Low > cfg.High {
		return nil, fmt.Errorf("%w: Low/High must satisfy 0 <= Low <= High <= 2^63 - 1, got [%d, %d]", ErrConfig, cfg.Low, cfg.High)
	}
	// n = high - low + 1 may be exactly 2^63 (low 0, high 2^63 - 1), which
	// overflows int64; high - low itself fits, so the sum is taken in uint64.
	n := uint64(cfg.High-cfg.Low) + 1
	if n < 100 {
		return nil, fmt.Errorf("%w: range must span at least 100 values (FF1 minimum domain), got %d", ErrConfig, n)
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

	radix, m, capacity := selectRangeDomain(n)
	f, err := newFF1(aesKey, radix)
	if err != nil {
		return nil, fmt.Errorf("%w: %v", ErrConfig, err)
	}

	c := &RangeCodec{
		low:      cfg.Low,
		high:     cfg.High,
		domain:   cfg.Domain,
		radix:    radix,
		m:        m,
		capacity: capacity,
		f:        f,
	}

	// The tweak is fixed for the life of the codec: "dealcode/v1r/" +
	// decimal(low) + "/" + decimal(high) + "/" + domain (SPEC §12.3), the
	// bounds rendered base-10 with no leading zeros ("0" for zero). Binding
	// low and high makes different ranges unrelated permutations, exactly as
	// domain does. With domain <= 255 bytes the tweak is at most 310 bytes.
	tweak := make([]byte, 0, len(rangeTweakPrefix)+2*19+2+len(cfg.Domain))
	tweak = append(tweak, rangeTweakPrefix...)
	tweak = strconv.AppendInt(tweak, cfg.Low, 10)
	tweak = append(tweak, '/')
	tweak = strconv.AppendInt(tweak, cfg.High, 10)
	tweak = append(tweak, '/')
	tweak = append(tweak, cfg.Domain...)

	// All FF1 parameters are validated above (m >= 2 and capacity >= 100 by
	// construction of selectRangeDomain), so this cannot fail; keep the
	// check for defence in depth.
	c.params, err = f.params(tweak, m)
	if err != nil {
		return nil, fmt.Errorf("%w: %v", ErrConfig, err)
	}
	return c, nil
}

// Low returns the smallest code the range may contain.
func (c *RangeCodec) Low() int64 { return c.low }

// High returns the largest code the range may contain. Codes at or above
// Low() + Capacity() — the dead zone — are never actually issued.
func (c *RangeCodec) High() int64 { return c.high }

// Domain returns the codec's namespace label.
func (c *RangeCodec) Domain() string { return c.domain }

// Radix returns the internal FF1 radix (SPEC §12.2); informational.
func (c *RangeCodec) Radix() int { return c.radix }

// Capacity returns the number of issuable codes: the largest radix^m (with
// radix <= 256) not exceeding High() - Low() + 1. It is a uint64 because the
// full-counter-space configuration [0, 2^63 - 1] derives capacity exactly
// 2^63, which overflows int64. Encode accepts exactly [0, Capacity()) —
// monitor counter consumption against Capacity(), not the raw span.
func (c *RangeCodec) Capacity() uint64 { return c.capacity }

// String describes the codec's public configuration. Key material never
// appears in the output.
func (c *RangeCodec) String() string {
	return fmt.Sprintf("dealcode.RangeCodec(low=%d, high=%d, domain=%q)",
		c.low, c.high, c.domain)
}

// Encode maps counter n to its integer code in [Low(), Low() + Capacity())
// (SPEC.md §12.3). It returns an error wrapping ErrRange when n is outside
// [0, Capacity()) — this mode has no staging and no cycles; when the range
// is exhausted, it is exhausted.
func (c *RangeCodec) Encode(n int64) (int64, error) {
	// uint64 arithmetic covers the capacity == 2^63 boundary configuration,
	// where every non-negative int64 counter is valid.
	if n < 0 || uint64(n) >= c.capacity {
		return 0, fmt.Errorf("%w: counter %d outside [0, %d)", ErrRange, n, c.capacity)
	}
	v := uint64(n)
	r := uint64(c.radix)
	numerals := make([]int, c.m)
	for i := c.m - 1; i >= 0; i-- {
		numerals[i] = int(v % r)
		v /= r
	}
	cipher := c.f.encrypt(c.params, numerals)
	var y uint64
	for _, x := range cipher {
		y = y*r + uint64(x)
	}
	// y < capacity <= 2^63 fits int64, and low + y <= high <= 2^63 - 1, so
	// the addition cannot overflow.
	return c.low + int64(y), nil
}

// Decode maps an integer code back to its counter (SPEC.md §12.3). A code
// this codec could never have issued — below Low(), above High(), or in the
// dead zone [Low() + Capacity(), High()] — yields an error wrapping
// ErrInvalidCode.
//
// Decode success only proves the code is consistent with the key and range;
// the application still decides whether counter n actually exists.
func (c *RangeCodec) Decode(code int64) (int64, error) {
	if code < c.low || code > c.high {
		return 0, fmt.Errorf("%w: code %d outside range [%d, %d]", ErrInvalidCode, code, c.low, c.high)
	}
	v := uint64(code - c.low)
	if v >= c.capacity {
		return 0, fmt.Errorf("%w: code %d in the unissued top slice of the range (capacity %d)", ErrInvalidCode, code, c.capacity)
	}
	r := uint64(c.radix)
	numerals := make([]int, c.m)
	for i := c.m - 1; i >= 0; i-- {
		numerals[i] = int(v % r)
		v /= r
	}
	plain := c.f.decrypt(c.params, numerals)
	var n uint64
	for _, x := range plain {
		n = n*r + uint64(x)
	}
	// n < capacity <= 2^63 always holds (FF1 permutes [0, radix^m)), so no
	// further range check is needed and n fits int64.
	return int64(n), nil
}

// selectRangeDomain derives (radix, m, capacity) per SPEC §12.2: the largest
// radix^m <= n with 2 <= radix <= 256 and 2 <= m <= 63, the smallest m
// winning among equal capacities. All arithmetic is exact — no floats. The
// loop always finds a candidate for n >= 100 (m = 2 gives radix >= 10 and
// capacity >= 100).
func selectRangeDomain(n uint64) (radix, m int, capacity uint64) {
	for cm := 2; cm <= 63; cm++ {
		r := cappedRoot(n, cm)
		if r < 2 {
			continue
		}
		c, ok := powWithin(r, cm, n)
		if !ok {
			// Unreachable: cappedRoot only returns r with r^cm <= n.
			panic("dealcode: internal error: cappedRoot overshot")
		}
		if c > capacity { // strict '>' keeps the smallest m on ties
			capacity = c
			radix = int(r)
			m = cm
		}
	}
	return radix, m, capacity
}

// cappedRoot returns min(iroot(n, m), 256) — the largest r in [1, 256] with
// r^m <= n — by binary search with overflow-guarded integer powers, exactly
// as SPEC §12.2 requires (floating-point roots are forbidden). The 256 cap
// folds the min into the search bounds.
func cappedRoot(n uint64, m int) uint64 {
	lo, hi := uint64(1), uint64(maxRangeRadix)+1 // 1^m <= n for all n >= 1
	for hi-lo > 1 {
		mid := (lo + hi) / 2
		if _, ok := powWithin(mid, m, n); ok {
			lo = mid
		} else {
			hi = mid
		}
	}
	return lo
}

// powWithin returns r^m when r^m <= n, and ok == false otherwise. The
// running product is bounds-checked against n before every multiplication
// (p <= n/r implies p*r <= n), so it can neither exceed n nor overflow
// uint64. r must be at least 1.
func powWithin(r uint64, m int, n uint64) (p uint64, ok bool) {
	p = 1
	for i := 0; i < m; i++ {
		if p > n/r {
			return 0, false
		}
		p *= r
	}
	return p, true
}
