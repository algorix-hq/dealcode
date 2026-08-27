"""Cross-language conformance: dealcode v1 test vectors."""

import pytest

from dealcode import Dealcode, InvalidCodeError


def _codec(cfg) -> Dealcode:
    alphabet = cfg.get("custom_alphabet") or cfg["alphabet"]
    key = cfg["key_string"] if "key_string" in cfg else bytes.fromhex(cfg["key_hex"])
    return Dealcode(
        key=key,
        alphabet=alphabet,
        min_length=cfg["min_length"],
        max_length=cfg["max_length"],
        domain=cfg["domain"],
    )


def test_encode_decode(v1_vectors):
    for cfg in v1_vectors["configs"]:
        codec = _codec(cfg)
        for vec in cfg["vectors"]:
            n = int(vec["n"])
            assert codec.encode(n) == vec["code"], f"{cfg['name']}: encode({n})"
            assert codec.decode(vec["code"]) == n, f"{cfg['name']}: decode"


def test_invalid_codes(v1_vectors):
    for cfg in v1_vectors["configs"]:
        codec = _codec(cfg)
        for bad in cfg["invalid_codes"]:
            with pytest.raises(InvalidCodeError):
                codec.decode(bad)


def test_normalization(v1_vectors):
    for cfg in v1_vectors["configs"]:
        codec = _codec(cfg)
        for case in cfg.get("normalize", []):
            assert codec.decode(case["input"]) == int(case["n"]), cfg["name"]
