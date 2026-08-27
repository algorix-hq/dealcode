// Package dealcode maps a non-negative integer counter (from a database
// sequence or any other source that never repeats) to a short, fixed-alphabet,
// random-looking string called a code, and back.
//
// The mapping is a bijection (a keyed permutation, FF1 format-preserving
// encryption per NIST SP 800-38G), so two different counters can never produce
// the same code: uniqueness of codes reduces entirely to uniqueness of
// counters. Codes start at a minimum length and grow one character at a time
// only when the current length is exhausted. Without the key, codes carry no
// usable order or volume information.
//
// This package implements format version 1 of the dealcode specification
// (SPEC.md at the repository root) and is byte-for-byte interoperable with the
// other language implementations in the same repository.
//
// A Codec is immutable and safe for concurrent use by multiple goroutines;
// create one per code namespace at startup and reuse it:
//
//	codec, err := dealcode.New(dealcode.Config{
//		KeyString: os.Getenv("DEALCODE_KEY"),
//		Domain:    "orders",
//	})
//	if err != nil {
//		log.Fatal(err)
//	}
//	code, err := codec.Encode(42)   // e.g. "4b71b7"
//	n, err := codec.Decode(code)    // 42
//
// Dealcode codes are not authentication tokens: the code space is small and an
// online attacker can guess valid codes at a rate proportional to
// issued/capacity. Rate-limit lookups, and use >=128-bit random tokens for
// anything security-critical.
package dealcode
