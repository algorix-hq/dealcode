"""Fixed-length cycling mode (SPEC §11): conformance + behaviour."""

import pytest

from dealcode import ConfigError, CyclingDealcode, InvalidCodeError, RangeError


def _codec(cfg) -> CyclingDealcode:
    alphabet = cfg.get("custom_alphabet") or cfg["alphabet"]
    key = cfg["key_string"] if "key_string" in cfg else bytes.fromhex(cfg["key_hex"])
    return CyclingDealcode(key=key, alphabet=alphabet, length=cfg["length"], domain=cfg["domain"])


def test_v1c_encode_decode(v1c_vectors):
    for cfg in v1c_vectors["configs"]:
        codec = _codec(cfg)
        assert codec.capacity == int(cfg["capacity"]), cfg["name"]
        assert codec.max_cycle == int(cfg["max_cycle"]), cfg["name"]
        for vec in cfg["vectors"]:
            n = int(vec["n"])
            assert codec.encode(n) == vec["code"], f"{cfg['name']}: encode({n})"
            assert codec.decode(vec["code"], n // codec.capacity) == n, f"{cfg['name']}: decode"


def test_v1c_invalid_codes(v1c_vectors):
    for cfg in v1c_vectors["configs"]:
        codec = _codec(cfg)
        for entry in cfg["invalid_codes"]:
            with pytest.raises(InvalidCodeError):
                codec.decode(entry["code"], int(entry["cycle"]))


def test_v1c_normalize(v1c_vectors):
    for cfg in v1c_vectors["configs"]:
        codec = _codec(cfg)
        for case in cfg["normalize"]:
            assert codec.decode(case["input"], int(case["cycle"])) == int(case["n"])


def test_v1c_range_counters(v1c_vectors):
    for cfg in v1c_vectors["configs"]:
        codec = _codec(cfg)
        for raw in cfg["range_counters"]:
            with pytest.raises(RangeError):
                codec.encode(int(raw))
        probe = codec.encode(0)
        for raw in cfg["invalid_cycles"]:
            with pytest.raises(RangeError):
                codec.decode(probe, int(raw))


def test_v1c_invalid_configs(v1c_vectors):
    for ic in v1c_vectors["invalid_configs"]:
        key = ic["key_string"] if "key_string" in ic else bytes.fromhex(ic["key_hex"])
        alphabet = ic.get("custom_alphabet") or ic.get("alphabet")
        with pytest.raises(ConfigError):
            CyclingDealcode(key, alphabet, length=ic["length"], domain=ic.get("domain", ""))


def test_full_cycle_is_a_permutation_and_cycles_differ():
    codec = CyclingDealcode("k", "dec", length=2)  # capacity 100
    cycles = []
    for e in range(3):
        codes = [codec.encode(e * 100 + v) for v in range(100)]
        assert len(set(codes)) == 100
        assert all(codec.decode(codes[v], e) == e * 100 + v for v in range(100))
        cycles.append(codes)
    # same space every cycle, refilled in a different order
    assert set(cycles[0]) == set(cycles[1]) == set(cycles[2])
    assert cycles[0] != cycles[1] and cycles[1] != cycles[2] and cycles[0] != cycles[2]


def test_final_partial_cycle_boundary():
    codec = CyclingDealcode("k", "dec", length=2)
    top = 2**63 - 1
    code = codec.encode(top)
    assert codec.decode(code, codec.cycle_of(top)) == top
    with pytest.raises(RangeError):
        codec.encode(2**63)


def test_wrong_cycle_gives_a_different_counter():
    codec = CyclingDealcode("k", "crockford", length=6)
    code = codec.encode(7)
    assert codec.decode(code, 0) == 7
    assert codec.decode(code, 1) != 7  # documented ambiguity: cycle is context
