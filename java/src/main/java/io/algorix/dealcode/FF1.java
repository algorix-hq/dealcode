package io.algorix.dealcode;

import java.math.BigInteger;
import java.security.GeneralSecurityException;
import java.util.Arrays;
import javax.crypto.Cipher;
import javax.crypto.spec.SecretKeySpec;

/**
 * FF1 format-preserving encryption (NIST SP 800-38G, Algorithms 7 and 8).
 * Internal.
 *
 * <p>Numeral strings are {@code int[]} whose elements lie in
 * {@code [0, radix)}; character conversion is the caller's concern. AES comes
 * from the JDK's JCE provider ({@code AES/ECB/NoPadding}); FF1 itself carries
 * no other dependencies.</p>
 *
 * <p><b>Thread safety.</b> {@link Cipher} instances are not thread-safe, so
 * each thread gets its own via a {@link ThreadLocal}. Everything else in this
 * class is immutable; instances are safe for unsynchronized concurrent use.</p>
 */
final class FF1 {

    private final int radix;
    private final BigInteger radixBig;
    private final ThreadLocal<Cipher> cipher;

    FF1(byte[] key, int radix) {
        int len = key.length;
        if (len != 16 && len != 24 && len != 32) {
            throw new IllegalArgumentException("FF1 key must be 16, 24, or 32 bytes (AES)");
        }
        if (radix < 2 || radix > (1 << 16)) {
            throw new IllegalArgumentException("FF1 radix must be in [2, 2^16]");
        }
        SecretKeySpec spec = new SecretKeySpec(key.clone(), "AES");
        this.cipher = ThreadLocal.withInitial(() -> {
            try {
                Cipher c = Cipher.getInstance("AES/ECB/NoPadding");
                c.init(Cipher.ENCRYPT_MODE, spec);
                return c;
            } catch (GeneralSecurityException e) {
                // AES/ECB/NoPadding is mandated by the JCE spec; unreachable
                // on a conforming JRE.
                throw new IllegalStateException("AES unavailable", e);
            }
        });
        this.radix = radix;
        this.radixBig = BigInteger.valueOf(radix);
    }

    /**
     * Precomputed per-(tweak, n) FF1 round parameters: the P block folded into
     * a constant Q prefix, byte/expansion widths, and the Feistel moduli.
     */
    static final class Params {
        final int n;
        final int u;
        final int v;
        /** b = ceil(ceil(v * log2(radix)) / 8), computed exactly. */
        final int b;
        /** dLen = 4 * ceil(b / 4) + 4 — bytes of S to interpret as y. */
        final int dLen;
        /** P || tweak || zero padding; each round appends [i] and NUM(half). */
        final byte[] qPrefix;
        final BigInteger modU;
        final BigInteger modV;

        private Params(int n, int u, int v, int b, int dLen, byte[] qPrefix,
                BigInteger modU, BigInteger modV) {
            this.n = n;
            this.u = u;
            this.v = v;
            this.b = b;
            this.dLen = dLen;
            this.qPrefix = qPrefix;
            this.modU = modU;
            this.modV = modV;
        }
    }

    /** Computes round parameters for a given tweak and message length. */
    Params params(byte[] tweak, int n) {
        if (n < 2 || BigInteger.valueOf(radix).pow(n).compareTo(BigInteger.valueOf(100)) < 0) {
            throw new IllegalArgumentException("FF1 message too short for radix " + radix);
        }
        int t = tweak.length;
        int u = n / 2;
        int v = n - u;
        BigInteger modU = radixBig.pow(u);
        BigInteger modV = radixBig.pow(v);
        // ceil(ceil(v * log2(radix)) / 8) — exactly, via the bit length of
        // radix^v - 1. Floating-point log MUST NOT be used (SPEC §6).
        int b = (modV.subtract(BigInteger.ONE).bitLength() + 7) / 8;
        int dLen = 4 * ((b + 3) / 4) + 4;
        byte[] p = new byte[16];
        p[0] = 1;
        p[1] = 2;
        p[2] = 1;
        p[3] = (byte) (radix >>> 16);
        p[4] = (byte) (radix >>> 8);
        p[5] = (byte) radix;
        p[6] = 10;
        p[7] = (byte) u;
        p[8] = (byte) (n >>> 24);
        p[9] = (byte) (n >>> 16);
        p[10] = (byte) (n >>> 8);
        p[11] = (byte) n;
        p[12] = (byte) (t >>> 24);
        p[13] = (byte) (t >>> 16);
        p[14] = (byte) (t >>> 8);
        p[15] = (byte) t;
        int pad = Math.floorMod(-t - b - 1, 16);
        byte[] qPrefix = new byte[16 + t + pad];
        System.arraycopy(p, 0, qPrefix, 0, 16);
        System.arraycopy(tweak, 0, qPrefix, 16, t);
        // remaining bytes are already zero
        return new Params(n, u, v, b, dLen, qPrefix, modU, modV);
    }

    /** FF1.Encrypt (Algorithm 7). {@code x} is a numeral array; returns one. */
    int[] encrypt(Params p, int[] x) {
        checkNumerals(x, p.n);
        Cipher ciph = cipher.get();
        int[] a = Arrays.copyOfRange(x, 0, p.u);
        int[] b = Arrays.copyOfRange(x, p.u, p.n);
        for (int i = 0; i < 10; i++) {
            BigInteger y = roundY(ciph, p, i, num(b));
            int m = (i % 2 == 0) ? p.u : p.v;
            BigInteger mod = (i % 2 == 0) ? p.modU : p.modV;
            BigInteger c = num(a).add(y).mod(mod);
            a = b;
            b = str(c, m);
        }
        return concat(a, b);
    }

    /** FF1.Decrypt (Algorithm 8). {@code x} is a numeral array; returns one. */
    int[] decrypt(Params p, int[] x) {
        checkNumerals(x, p.n);
        Cipher ciph = cipher.get();
        int[] a = Arrays.copyOfRange(x, 0, p.u);
        int[] b = Arrays.copyOfRange(x, p.u, p.n);
        for (int i = 9; i >= 0; i--) {
            BigInteger y = roundY(ciph, p, i, num(a));
            int m = (i % 2 == 0) ? p.u : p.v;
            BigInteger mod = (i % 2 == 0) ? p.modU : p.modV;
            BigInteger c = num(b).subtract(y).mod(mod);
            b = a;
            a = str(c, m);
        }
        return concat(a, b);
    }

    // -- round function ------------------------------------------------------

    /**
     * One Feistel round's y: R = PRF(Q); S = R || CIPH(R xor [1]^16) || ...
     * truncated to dLen bytes (the general expansion loop); y = NUM(S).
     */
    private BigInteger roundY(Cipher ciph, Params p, int round, BigInteger numVal) {
        byte[] q = new byte[p.qPrefix.length + 1 + p.b];
        System.arraycopy(p.qPrefix, 0, q, 0, p.qPrefix.length);
        q[p.qPrefix.length] = (byte) round;
        toFixedBytes(numVal, q, p.qPrefix.length + 1, p.b);
        byte[] r = prf(ciph, q);
        byte[] s = r;
        for (long j = 1; s.length < p.dLen; j++) {
            byte[] block = r.clone();
            // xor with [j]^16, i.e. j as a 16-byte big-endian integer
            for (int k = 0; k < 8; k++) {
                block[15 - k] ^= (byte) (j >>> (8 * k));
            }
            byte[] extended = new byte[s.length + 16];
            System.arraycopy(s, 0, extended, 0, s.length);
            System.arraycopy(ciphBlock(ciph, block), 0, extended, s.length, 16);
            s = extended;
        }
        return new BigInteger(1, Arrays.copyOf(s, p.dLen));
    }

    /** PRF: AES-CBC-MAC with a zero IV; data length is a multiple of 16. */
    private static byte[] prf(Cipher ciph, byte[] data) {
        byte[] r = new byte[16];
        byte[] block = new byte[16];
        for (int off = 0; off < data.length; off += 16) {
            for (int k = 0; k < 16; k++) {
                block[k] = (byte) (r[k] ^ data[off + k]);
            }
            r = ciphBlock(ciph, block);
        }
        return r;
    }

    private static byte[] ciphBlock(Cipher ciph, byte[] block) {
        try {
            return ciph.doFinal(block);
        } catch (GeneralSecurityException e) {
            throw new IllegalStateException("AES failure", e); // unreachable: 16-byte input
        }
    }

    // -- numeral helpers -------------------------------------------------------

    /** NUM(x): the integer the numeral array represents, big-endian. */
    private BigInteger num(int[] xs) {
        BigInteger acc = BigInteger.ZERO;
        for (int x : xs) {
            acc = acc.multiply(radixBig).add(BigInteger.valueOf(x));
        }
        return acc;
    }

    /** STR(value, m): value as exactly m base-radix numerals, big-endian. */
    private int[] str(BigInteger value, int m) {
        int[] out = new int[m];
        for (int i = m - 1; i >= 0; i--) {
            BigInteger[] qr = value.divideAndRemainder(radixBig);
            value = qr[0];
            out[i] = qr[1].intValue();
        }
        return out;
    }

    /** Writes {@code value} as exactly {@code len} big-endian bytes. */
    private static void toFixedBytes(BigInteger value, byte[] dest, int off, int len) {
        byte[] raw = value.toByteArray(); // may carry a leading zero or be short
        int start = raw.length > len ? raw.length - len : 0;
        int copyLen = raw.length - start;
        System.arraycopy(raw, start, dest, off + (len - copyLen), copyLen);
    }

    private void checkNumerals(int[] x, int n) {
        if (x.length != n) {
            throw new IllegalArgumentException(
                    "numeral string length " + x.length + " != expected " + n);
        }
        for (int xi : x) {
            if (xi < 0 || xi >= radix) {
                throw new IllegalArgumentException("numeral out of range for radix " + radix);
            }
        }
    }

    private static int[] concat(int[] a, int[] b) {
        int[] out = new int[a.length + b.length];
        System.arraycopy(a, 0, out, 0, a.length);
        System.arraycopy(b, 0, out, a.length, b.length);
        return out;
    }
}
