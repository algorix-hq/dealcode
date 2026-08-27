package dealcode

// FF1 format-preserving encryption (NIST SP 800-38G, Algorithms 7 and 8),
// implemented directly from the NIST specification and validated against the
// official NIST FF1-AES sample vectors (testvectors/ff1_nist.json).
//
// Numeral strings are represented as []int with each element in [0, radix);
// conversion to and from characters is the caller's concern.

import (
	"crypto/aes"
	"crypto/cipher"
	"encoding/binary"
	"errors"
	"math/big"
)

// ff1 is an FF1-AES context for one key and radix. The embedded cipher.Block
// returned by aes.NewCipher is safe for concurrent use, and every other field
// is read-only after construction, so an ff1 may be shared by any number of
// goroutines.
type ff1 struct {
	block    cipher.Block
	radix    int
	radixBig *big.Int
}

func newFF1(key []byte, radix int) (*ff1, error) {
	if radix < 2 || radix > 1<<16 {
		return nil, errors.New("ff1: radix must be in [2, 2^16]")
	}
	block, err := aes.NewCipher(key)
	if err != nil {
		return nil, err
	}
	return &ff1{block: block, radix: radix, radixBig: big.NewInt(int64(radix))}, nil
}

// ff1Params holds every value that depends only on (tweak, message length):
// the split point, byte sizes, the constant prefix of the CBC-MAC input Q
// (P || tweak || zero padding), and the Feistel moduli radix^u and radix^v.
// All fields are read-only after construction.
type ff1Params struct {
	u, v    int      // left/right half lengths: u = n/2, v = n - u
	b       int      // byte length of NUM(B): ceil(ceil(v*log2(radix))/8)
	d       int      // 4*ceil(b/4) + 4
	qPrefix []byte   // P || tweak || zero pad; each round appends [i] || NUM(b bytes)
	modU    *big.Int // radix^u
	modV    *big.Int // radix^v
}

// params computes the per-(tweak, length) constants. It rejects messages
// shorter than 2 numerals or with a domain radix^n < 100 (the FF1 structural
// minimum).
func (f *ff1) params(tweak []byte, n int) (*ff1Params, error) {
	if n < 2 {
		return nil, errors.New("ff1: message must have at least 2 numerals")
	}
	domain := new(big.Int).Exp(f.radixBig, big.NewInt(int64(n)), nil)
	if domain.Cmp(big.NewInt(100)) < 0 {
		return nil, errors.New("ff1: radix^n must be at least 100")
	}
	u := n / 2
	v := n - u
	modU := new(big.Int).Exp(f.radixBig, big.NewInt(int64(u)), nil)
	modV := new(big.Int).Exp(f.radixBig, big.NewInt(int64(v)), nil)
	// b = ceil(ceil(v*log2(radix))/8), computed exactly as the bit length of
	// radix^v - 1 — floating-point log must not be used.
	b := (new(big.Int).Sub(modV, big.NewInt(1)).BitLen() + 7) / 8
	d := 4*((b+3)/4) + 4

	t := len(tweak)
	pad := (((-t - b - 1) % 16) + 16) % 16
	q := make([]byte, 0, 16+t+pad)
	// P = [1] [2] [1] [radix]^3 [10] [u mod 256] [n]^4 [t]^4
	q = append(q, 1, 2, 1, byte(f.radix>>16), byte(f.radix>>8), byte(f.radix), 10, byte(u))
	q = binary.BigEndian.AppendUint32(q, uint32(n))
	q = binary.BigEndian.AppendUint32(q, uint32(t))
	q = append(q, tweak...)
	q = append(q, make([]byte, pad)...)
	return &ff1Params{u: u, v: v, b: b, d: d, qPrefix: q, modU: modU, modV: modV}, nil
}

// roundY computes the round value y = NUM(S[:d]) for round i and the numeral
// value numVal (NUM of the half that feeds the round function). q is a
// caller-owned scratch buffer holding qPrefix in its first prefixLen bytes
// with room for [i] and the b-byte numeral value after it.
func (f *ff1) roundY(q []byte, prefixLen int, round byte, numVal *big.Int, d int, y *big.Int) {
	q[prefixLen] = round
	numVal.FillBytes(q[prefixLen+1:]) // zero-pads on the left
	// R = PRF(Q): CBC-MAC with a zero IV.
	var r, blk [16]byte
	for off := 0; off < len(q); off += 16 {
		for j := 0; j < 16; j++ {
			blk[j] = r[j] ^ q[off+j]
		}
		f.block.Encrypt(r[:], blk[:])
	}
	if d <= 16 {
		// Within dealcode's configuration bounds d <= 16 always holds, so the
		// S expansion below never runs; it is implemented anyway so the FF1
		// core is fully general.
		y.SetBytes(r[:d])
		return
	}
	// S = R || CIPH(R xor [1]^16) || CIPH(R xor [2]^16) || ..., truncated to d.
	s := make([]byte, 0, ((d+15)/16)*16)
	s = append(s, r[:]...)
	var jb, out [16]byte
	for j := uint64(1); len(s) < d; j++ {
		binary.BigEndian.PutUint64(jb[8:], j)
		for k := 0; k < 16; k++ {
			blk[k] = r[k] ^ jb[k]
		}
		f.block.Encrypt(out[:], blk[:])
		s = append(s, out[:]...)
	}
	y.SetBytes(s[:d])
}

// num sets acc to NUM(xs) — the big-endian base-radix value of the numeral
// string — and returns acc. tmp is scratch.
func (f *ff1) num(xs []int, acc, tmp *big.Int) *big.Int {
	acc.SetInt64(0)
	for _, x := range xs {
		acc.Mul(acc, f.radixBig)
		acc.Add(acc, tmp.SetInt64(int64(x)))
	}
	return acc
}

// nstr writes STR(val, radix, len(out)) — val as exactly len(out) big-endian
// base-radix numerals — into out. val and rem are consumed as scratch.
func (f *ff1) nstr(val *big.Int, out []int, rem *big.Int) {
	for i := len(out) - 1; i >= 0; i-- {
		val.QuoRem(val, f.radixBig, rem)
		out[i] = int(rem.Int64())
	}
}

// encrypt is FF1.Encrypt (Algorithm 7) for the precomputed params p.
// x must have exactly p.u+p.v numerals, each in [0, radix).
func (f *ff1) encrypt(p *ff1Params, x []int) []int {
	u, v := p.u, p.v
	// Three rotating numeral buffers; v >= u so capacity v fits either half.
	a := append(make([]int, 0, v), x[:u]...)
	b := append(make([]int, 0, v), x[u:]...)
	t := make([]int, 0, v)

	q := make([]byte, len(p.qPrefix)+1+p.b)
	copy(q, p.qPrefix)

	var numA, numB, y, c, tmp big.Int
	for i := 0; i < 10; i++ {
		f.roundY(q, len(p.qPrefix), byte(i), f.num(b, &numB, &tmp), p.d, &y)
		m, mod := u, p.modU
		if i%2 == 1 {
			m, mod = v, p.modV
		}
		f.num(a, &numA, &tmp)
		c.Add(&numA, &y)
		c.Mod(&c, mod)
		nb := t[:m]
		f.nstr(&c, nb, &tmp)
		a, b, t = b, nb, a[:0]
	}
	out := make([]int, 0, u+v)
	out = append(out, a...)
	return append(out, b...)
}

// decrypt is FF1.Decrypt (Algorithm 8) for the precomputed params p.
// x must have exactly p.u+p.v numerals, each in [0, radix).
func (f *ff1) decrypt(p *ff1Params, x []int) []int {
	u, v := p.u, p.v
	a := append(make([]int, 0, v), x[:u]...)
	b := append(make([]int, 0, v), x[u:]...)
	t := make([]int, 0, v)

	q := make([]byte, len(p.qPrefix)+1+p.b)
	copy(q, p.qPrefix)

	var numA, numB, y, c, tmp big.Int
	for i := 9; i >= 0; i-- {
		f.roundY(q, len(p.qPrefix), byte(i), f.num(a, &numA, &tmp), p.d, &y)
		m, mod := u, p.modU
		if i%2 == 1 {
			m, mod = v, p.modV
		}
		f.num(b, &numB, &tmp)
		c.Sub(&numB, &y)
		c.Mod(&c, mod) // Euclidean: result is always in [0, mod)
		na := t[:m]
		f.nstr(&c, na, &tmp)
		b, a, t = a, na, b[:0]
	}
	out := make([]int, 0, u+v)
	out = append(out, a...)
	return append(out, b...)
}
