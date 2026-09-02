"""Integer range mode (SPEC §12): conformance + behaviour."""

import pytest

from dealcode import ConfigError, InvalidCodeError, RangeDealcode, RangeError
from dealcode._range import _select_domain


def _codec(cfg) -> RangeDealcode:
    key = cfg["key_string"] if "key_string" in cfg else bytes.fromhex(cfg["key_hex"])
    return RangeDealcode(
        key=key, low=int(cfg["low"]), high=int(cfg["high"]), domain=cfg["domain"]
    )


def test_v1r_encode_decode(v1r_vectors):
    for cfg in v1r_vectors["configs"]:
        codec = _codec(cfg)
        assert codec.radix == cfg["radix"], cfg["name"]
        assert codec.capacity == int(cfg["capacity"]), cfg["name"]
        for vec in cfg["vectors"]:
            n, code = int(vec["n"]), int(vec["code"])
            assert codec.encode(n) == code, f"{cfg['name']}: encode({n})"
            assert codec.decode(code) == n, f"{cfg['name']}: decode({code})"


def test_v1r_invalid_codes(v1r_vectors):
    for cfg in v1r_vectors["configs"]:
        codec = _codec(cfg)
        for bad in cfg["invalid_codes"]:
            with pytest.raises(InvalidCodeError):
                codec.decode(int(bad))


def test_v1r_range_counters(v1r_vectors):
    for cfg in v1r_vectors["configs"]:
        codec = _codec(cfg)
        for raw in cfg["range_counters"]:
            with pytest.raises(RangeError):
                codec.encode(int(raw))


def test_v1r_invalid_configs(v1r_vectors):
    for ic in v1r_vectors["invalid_configs"]:
        key = ic["key_string"] if "key_string" in ic else bytes.fromhex(ic["key_hex"])
        with pytest.raises(ConfigError):
            RangeDealcode(
                key, low=int(ic["low"]), high=int(ic["high"]), domain=ic.get("domain", "")
            )


# --- domain selection (SPEC §12.2) -----------------------------------------


def test_domain_selection_known_values():
    assert _select_domain(100) == (10, 2, 100)
    assert _select_domain(900_000) == (96, 3, 884_736)
    assert _select_domain(1_000_000) == (100, 3, 1_000_000)  # exact power
    assert _select_domain(2**63) == (128, 9, 2**63)  # exact power at the bound
    assert _select_domain(65_536) == (256, 2, 65_536)  # tie -> smallest m


def test_domain_selection_capacity_bounds():
    # capacity <= N always; > 96% for N >= 10^5 (SPEC §12.2)
    for n in (100, 101, 999, 10_000, 99_999, 10**5, 900_000, 10**7 + 3,
              2**32, 2**53 + 1, 2**63 - 1, 2**63):
        radix, m, cap = _select_domain(n)
        assert 2 <= radix <= 256 and 2 <= m <= 63
        assert radix**m == cap <= n
        assert (radix + 1) ** m > n or radix == 256
        if n >= 10**5:
            assert cap / n > 0.96, n


# --- behaviour --------------------------------------------------------------


def test_small_range_is_a_bijection():
    codec = RangeDealcode(key="k", low=1_000, high=1_120)  # N=121 = 11^2, no dead zone
    assert codec.capacity == 121
    codes = [codec.encode(n) for n in range(121)]
    assert sorted(codes) == list(range(1_000, 1_121))
    assert [codec.decode(c) for c in codes] == list(range(121))


def test_dead_zone_rejected_but_issued_top_accepted():
    codec = RangeDealcode(key="k", low=100_000, high=999_999)
    top_issued = codec.low + codec.capacity - 1  # 984735
    assert codec.decode(codec.encode(codec.capacity - 1)) == codec.capacity - 1
    assert 100_000 <= codec.encode(codec.capacity - 1) <= top_issued
    for dead in (top_issued + 1, 999_999):
        with pytest.raises(InvalidCodeError):
            codec.decode(dead)


def test_range_and_domain_bind_the_permutation():
    a = RangeDealcode(key="k", low=100_000, high=999_999)
    b = RangeDealcode(key="k", low=100_000, high=999_998)
    c = RangeDealcode(key="k", low=100_000, high=999_999, domain="x")
    outs = {tuple(x.encode(n) for n in range(8)) for x in (a, b, c)}
    assert len(outs) == 3


def test_type_strictness():
    codec = RangeDealcode(key="k", low=100_000, high=999_999)
    for bad in (True, 1.0, "5"):
        with pytest.raises(RangeError):
            codec.encode(bad)
        with pytest.raises(InvalidCodeError):
            codec.decode(bad if not isinstance(bad, str) else "500000")
    with pytest.raises(ConfigError):
        RangeDealcode(key="k", low=True, high=999_999)


def test_repr_hides_key():
    codec = RangeDealcode(key="super-secret", low=100_000, high=999_999, domain="d")
    assert "secret" not in repr(codec)
