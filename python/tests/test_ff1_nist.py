"""FF1 core must reproduce the official NIST sample vectors exactly."""

from dealcode import FF1

_BASE36 = "0123456789abcdefghijklmnopqrstuvwxyz"


def _numerals(s: str) -> list[int]:
    return [_BASE36.index(c) for c in s]


def _string(xs) -> str:
    return "".join(_BASE36[x] for x in xs)


def test_nist_samples(nist_vectors):
    for vec in nist_vectors:
        ff1 = FF1(bytes.fromhex(vec["key_hex"]), vec["radix"])
        tweak = bytes.fromhex(vec["tweak_hex"])
        ct = ff1.encrypt(tweak, _numerals(vec["plaintext"]))
        assert _string(ct) == vec["ciphertext"], f"sample {vec['sample']} encrypt"
        pt = ff1.decrypt(tweak, _numerals(vec["ciphertext"]))
        assert _string(pt) == vec["plaintext"], f"sample {vec['sample']} decrypt"
