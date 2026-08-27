"""FF1 format-preserving encryption (NIST SP 800-38G, Algorithms 7 and 8).

Implemented directly from the NIST specification and validated against the
official NIST FF1-AES sample vectors (``testvectors/ff1_nist.json``).

AES comes from the ``cryptography`` package (PyCA); FF1 itself carries no
other dependencies.
"""

from __future__ import annotations

import threading

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

__all__ = ["FF1"]


class FF1:
    """FF1 over numeral strings.

    Numeral strings are sequences of integers in ``[0, radix)``; conversion
    to/from characters is the caller's concern.
    """

    def __init__(self, key: bytes, radix: int) -> None:
        key = bytes(key)
        if len(key) not in (16, 24, 32):
            raise ValueError("FF1 key must be 16, 24, or 32 bytes (AES)")
        if not (2 <= radix <= 2**16):
            raise ValueError("FF1 radix must be in [2, 2^16]")
        # ECB is stateless, so a single streaming encryptor can serve every
        # block-encryption need (including CBC-MAC, chained manually below).
        self._ecb = Cipher(algorithms.AES(key), modes.ECB()).encryptor()
        self.radix = radix
        self._param_cache: dict[tuple[bytes, int], tuple] = {}
        # The shared ECB context is not safe for concurrent use; codecs are
        # meant to be long-lived and shared, so serialize the hot section.
        self._lock = threading.Lock()

    # -- block cipher primitives ------------------------------------------

    def _ciph(self, block: bytes) -> bytes:
        return self._ecb.update(block)

    def _prf(self, data: bytes) -> bytes:
        # CBC-MAC with a zero IV.
        update = self._ecb.update
        r = bytes(16)
        for i in range(0, len(data), 16):
            block = data[i : i + 16]
            r = update(bytes(a ^ b for a, b in zip(r, block)))
        return r

    # -- shared round setup ------------------------------------------------

    def _params(self, tweak: bytes, n: int):
        try:
            return self._param_cache[(tweak, n)]
        except KeyError:
            pass
        radix = self.radix
        if n < 2 or radix**n < 100:
            raise ValueError("FF1 message too short for radix")
        t = len(tweak)
        u = n // 2
        v = n - u
        # ceil(ceil(v * log2(radix)) / 8), computed exactly.
        b = ((radix**v - 1).bit_length() + 7) // 8
        d = 4 * ((b + 3) // 4) + 4
        p = (
            bytes([1, 2, 1])
            + radix.to_bytes(3, "big")
            + bytes([10, u & 0xFF])
            + n.to_bytes(4, "big")
            + t.to_bytes(4, "big")
        )
        q_prefix = p + tweak + b"\x00" * ((-t - b - 1) % 16)
        params = (u, v, b, d, q_prefix, radix**u, radix**v)
        self._param_cache[(tweak, n)] = params
        return params

    def _round_y(self, q_prefix: bytes, b: int, d: int, i: int, num_val: int) -> int:
        r = self._prf(q_prefix + bytes([i]) + num_val.to_bytes(b, "big"))
        s = r
        j = 1
        while len(s) < d:
            s += self._ciph(bytes(x ^ y for x, y in zip(r, j.to_bytes(16, "big"))))
            j += 1
        return int.from_bytes(s[:d], "big")

    # -- numeral helpers ---------------------------------------------------

    def _num(self, xs) -> int:
        acc = 0
        for x in xs:
            acc = acc * self.radix + x
        return acc

    def _str(self, value: int, m: int):
        radix = self.radix
        out = [0] * m
        for i in range(m - 1, -1, -1):
            value, out[i] = divmod(value, radix)
        return out

    # -- public API ---------------------------------------------------------

    def encrypt(self, tweak: bytes, x):
        """FF1.Encrypt (Algorithm 7). ``x`` is a numeral list; returns one."""
        radix = self.radix
        if any(not (0 <= xi < radix) for xi in x):
            raise ValueError("numeral out of range")
        n = len(x)
        u, v, b, d, q_prefix, mod_u, mod_v = self._params(tweak, n)
        a, bb = list(x[:u]), list(x[u:])
        with self._lock:
            for i in range(10):
                y = self._round_y(q_prefix, b, d, i, self._num(bb))
                m, mod = (u, mod_u) if i % 2 == 0 else (v, mod_v)
                c = (self._num(a) + y) % mod
                a, bb = bb, self._str(c, m)
        return a + bb

    def decrypt(self, tweak: bytes, x):
        """FF1.Decrypt (Algorithm 8). ``x`` is a numeral list; returns one."""
        radix = self.radix
        if any(not (0 <= xi < radix) for xi in x):
            raise ValueError("numeral out of range")
        n = len(x)
        u, v, b, d, q_prefix, mod_u, mod_v = self._params(tweak, n)
        a, bb = list(x[:u]), list(x[u:])
        with self._lock:
            for i in range(9, -1, -1):
                y = self._round_y(q_prefix, b, d, i, self._num(a))
                m, mod = (u, mod_u) if i % 2 == 0 else (v, mod_v)
                c = (self._num(bb) - y) % mod
                bb, a = a, self._str(c, m)
        return a + bb
