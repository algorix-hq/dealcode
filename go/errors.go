package dealcode

import "errors"

// Sentinel errors. Every error returned by this package wraps exactly one of
// these, so callers can classify failures with errors.Is while the returned
// error itself carries a descriptive message:
//
//	n, err := codec.Decode(input)
//	if errors.Is(err, dealcode.ErrInvalidCode) {
//		// input was never issued by this codec
//	}
var (
	// ErrConfig reports an invalid codec configuration: bad key material,
	// alphabet, lengths, or domain. It is returned only by New.
	ErrConfig = errors.New("dealcode: invalid configuration")

	// ErrRange reports an Encode counter outside [0, Capacity()).
	ErrRange = errors.New("dealcode: counter out of range")

	// ErrInvalidCode reports a Decode input that fails length, charset, or
	// stage-range validation — i.e. a string this codec never issued.
	ErrInvalidCode = errors.New("dealcode: invalid code")
)
