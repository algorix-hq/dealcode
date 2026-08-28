"""Behavioural tests for the codec layer (beyond shared vectors)."""

import pytest

from dealcode import ConfigError, Dealcode, InvalidCodeError, RangeError

KEY = bytes(range(16))


def test_roundtrip_dense_and_boundaries():
    codec = Dealcode(KEY, "hex", min_length=6)
    seen = set()
    boundary = [0, 1, 16**6 - 1, 16**6, 16**6 + 1, 16**7 - 1, 16**7, 16**15 - 1]
    for n in list(range(2000)) + boundary:
        code = codec.encode(n)
        assert len(code) == max(6, len(f"{n:x}"))
        assert codec.decode(code) == n
        seen.add(code)
    assert len(seen) == len(set(list(range(2000)) + boundary))


def test_stage_lengths():
    codec = Dealcode(KEY, "dec", min_length=6)
    assert len(codec.encode(999_999)) == 6
    assert len(codec.encode(1_000_000)) == 7
    assert len(codec.encode(9_999_999)) == 7
    assert len(codec.encode(10_000_000)) == 8


def test_domain_separation():
    a = Dealcode(KEY, "hex", domain="orders")
    b = Dealcode(KEY, "hex", domain="coupons")
    plain = Dealcode(KEY, "hex")
    assert a.encode(12345) != b.encode(12345)
    assert a.encode(12345) != plain.encode(12345)


def test_capacity_and_range_errors():
    codec = Dealcode(KEY, "hex")
    assert codec.capacity == 16**15
    with pytest.raises(RangeError):
        codec.encode(-1)
    with pytest.raises(RangeError):
        codec.encode(codec.capacity)
    with pytest.raises(RangeError):
        codec.encode("42")  # type: ignore[arg-type]
    codec.encode(codec.capacity - 1)  # must not raise


def test_decode_validation():
    codec = Dealcode(KEY, "hex")
    with pytest.raises(InvalidCodeError):
        codec.decode("abc")  # too short
    with pytest.raises(InvalidCodeError):
        codec.decode("a" * 16)  # too long
    with pytest.raises(InvalidCodeError):
        codec.decode("zzzzzz")  # bad chars
    with pytest.raises(InvalidCodeError):
        codec.decode(123456)  # type: ignore[arg-type]


def test_hex_case_insensitive_decode():
    codec = Dealcode(KEY, "hex")
    code = codec.encode(42)
    assert codec.decode(code.upper()) == 42


def test_crockford_confusables():
    codec = Dealcode(KEY, "crockford")
    n = 1_234_567
    code = codec.encode(n)
    assert codec.decode(code.lower()) == n
    swapped = code.replace("0", "O").replace("1", "I")
    assert codec.decode(swapped) == n


def test_fixed_length_mode():
    codec = Dealcode(KEY, "hex", min_length=8, max_length=8)
    assert codec.capacity == 16**8
    assert len(codec.encode(codec.capacity - 1)) == 8


def test_custom_alphabet():
    codec = Dealcode(KEY, "BCDFGHJKLMNPQRSTVWXZ", min_length=6)
    n = 987_654
    code = codec.encode(n)
    assert set(code) <= set("BCDFGHJKLMNPQRSTVWXZ")
    assert codec.decode(code) == n


def test_config_errors():
    with pytest.raises(ConfigError):
        Dealcode(b"")  # empty key
    with pytest.raises(ConfigError):
        Dealcode("")  # empty string key
    with pytest.raises(ConfigError):
        Dealcode(KEY, "01", min_length=6)  # 2^6 = 64 < 100
    with pytest.raises(ConfigError):
        Dealcode(KEY, "hex", min_length=6, max_length=5)
    with pytest.raises(ConfigError):
        Dealcode(KEY, "hex", max_length=33)  # 16^33 > 2^128
    with pytest.raises(ConfigError):
        Dealcode(KEY, "aa")  # duplicate chars
    with pytest.raises(ConfigError):
        Dealcode(KEY, "ab cd")  # space not allowed
    with pytest.raises(ConfigError):
        Dealcode(KEY, "hex", domain="x" * 256)
    # absurd lengths must be rejected in O(1), before any big-power work
    import time

    t0 = time.perf_counter()
    with pytest.raises(ConfigError):
        Dealcode(KEY, "hex", max_length=2**31 - 1)
    with pytest.raises(ConfigError):
        Dealcode(KEY, "hex", min_length=10**9)
    assert time.perf_counter() - t0 < 0.1
    # U+0000 and unpaired surrogates in string inputs are rejected, not mangled
    with pytest.raises(ConfigError):
        Dealcode(KEY, "hex", domain="a\x00b")
    with pytest.raises(ConfigError):
        Dealcode(KEY, "hex", domain="\ud800")
    with pytest.raises(ConfigError):
        Dealcode("a\x00b")
    with pytest.raises(ConfigError):
        Dealcode("\udfff")


def test_preset_lookalike_alphabet_rejected():
    # ASCII-lowercase of a custom alphabet matching a preset name is a footgun
    with pytest.raises(ConfigError, match="matches the preset name"):
        Dealcode(KEY, "HEX")
    with pytest.raises(ConfigError, match='pass "crockford" for the preset'):
        Dealcode(KEY, "Crockford")
    with pytest.raises(ConfigError, match="matches the preset name"):
        Dealcode(KEY, "Base64URL")
    # exact preset names still resolve as presets
    assert Dealcode(KEY, "hex").alphabet == "0123456789abcdef"
    # a genuinely custom alphabet is unaffected
    assert Dealcode(KEY, "BCDFGHJKLMNPQRSTVWXZ", min_length=6).radix == 20


def test_preset_name_as_string_key_rejected():
    # a string key that is a preset alphabet name means swapped arguments
    with pytest.raises(ConfigError, match="did you swap the key and alphabet"):
        Dealcode("crockford")
    with pytest.raises(ConfigError, match="preset alphabet name"):
        Dealcode("HEX")  # ASCII-lowercase matches too
    # bytes keys are unaffected
    assert Dealcode(b"crockford").decode(Dealcode(b"crockford").encode(7)) == 7


def test_unhashable_alphabet_raises_config_error():
    # a non-str, unhashable alphabet must not leak a bare TypeError
    with pytest.raises(ConfigError):
        Dealcode(KEY, ["a", "b", "c"])  # type: ignore[arg-type]
    with pytest.raises(ConfigError):
        Dealcode(KEY, 123)  # type: ignore[arg-type]


def test_key_material_flexibility():
    # openssl rand -hex 32 style string, passphrase, odd-length bytes: all work
    hex_str = "9f" * 32
    for key in (hex_str, "my passphrase", b"\x01\x02\x03\x04\x05", bytes(20)):
        codec = Dealcode(key)
        assert codec.decode(codec.encode(123)) == 123
    # a string key is derived from its UTF-8 bytes, never hex-decoded
    assert Dealcode(hex_str).encode(1) != Dealcode(bytes.fromhex(hex_str)).encode(1)
    # same material, same codes
    assert Dealcode("k1").encode(9) == Dealcode("k1").encode(9)


def test_small_domain_and_long_codes():
    # 4-digit decimal codes (10^4 domain, below the old 1e6 floor)
    pin = Dealcode(KEY, "dec", min_length=4, max_length=6)
    codes = {pin.encode(n) for n in range(10_000)}
    assert len(codes) == 10_000
    assert all(len(c) == 4 for c in codes)
    # code space larger than the counter space (16^16 > 2^63)
    wide = Dealcode(KEY, "hex", min_length=16, max_length=16)
    assert wide.capacity == 2**63
    code = wide.encode(2**63 - 1)
    assert len(code) == 16
    assert wide.decode(code) == 2**63 - 1
    with pytest.raises(RangeError):
        wide.encode(2**63)


def test_bijectivity_within_stage_sample():
    codec = Dealcode(KEY, "dec", min_length=6)
    codes = {codec.encode(n) for n in range(5000)}
    assert len(codes) == 5000
