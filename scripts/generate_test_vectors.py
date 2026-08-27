#!/usr/bin/env python3
"""Generate testvectors/v1.json from the Python reference implementation.

Run from the repository root:  python3 scripts/generate_test_vectors.py
"""

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python" / "src"))

from dealcode import Dealcode  # noqa: E402

COUNTER_BOUND = 2**63

KEY128 = bytes.fromhex("000102030405060708090a0b0c0d0e0f")
KEY192 = bytes.fromhex("000102030405060708090a0b0c0d0e0f1011121314151617")
KEY256 = bytes.fromhex(
    "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"
)
KEY20B = bytes.fromhex("000102030405060708090a0b0c0d0e0f10111213")  # derived
KEY_STR = "correct horse battery staple"  # derived from UTF-8

PRESET_NAMES = (
    "dec", "hex", "base32", "crockford", "base36", "base58", "base62", "base64url"
)

CONFIGS = [
    dict(name="hex-default", key=KEY128, alphabet="hex", min_length=6, domain=""),
    dict(name="hex-domain-orders", key=KEY128, alphabet="hex", min_length=6, domain="orders"),
    dict(name="hex-aes256", key=KEY256, alphabet="hex", min_length=6, domain=""),
    dict(name="hex-string-key", key=KEY_STR, alphabet="hex", min_length=6, domain=""),
    dict(name="hex-derived-20b-key", key=KEY20B, alphabet="hex", min_length=6, domain=""),
    dict(name="hex-fixed-16", key=KEY128, alphabet="hex", min_length=16, max_length=16, domain=""),
    dict(name="dec", key=KEY192, alphabet="dec", min_length=6, domain=""),
    dict(name="dec-4", key=KEY128, alphabet="dec", min_length=4, max_length=6, domain="pin"),
    dict(name="crockford", key=KEY256, alphabet="crockford", min_length=6, domain=""),
    dict(name="base32", key=KEY192, alphabet="base32", min_length=6, domain="tickets"),
    dict(name="base58", key=KEY128, alphabet="base58", min_length=5, domain=""),
    dict(name="base62-long", key=KEY256, alphabet="base62", min_length=4, max_length=12, domain="short"),
    dict(name="base64url", key=KEY128, alphabet="base64url", min_length=4, domain=""),
    dict(name="custom-consonants", key=KEY192, alphabet="BCDFGHJKLMNPQRSTVWXZ", min_length=6, domain=""),
    dict(name="hex-fixed-8", key=KEY128, alphabet="hex", min_length=8, max_length=8, domain=""),
]


def samples(lo: int, hi: int, count: int = 3):
    """Deterministic pseudo-random values in [lo, hi)."""
    span = hi - lo
    for i in range(1, count + 1):
        yield lo + (span * i * 2654435761) % span


def counters_for(codec: Dealcode):
    r, m, M = codec.radix, codec.min_length, codec.max_length
    cap = codec.capacity
    ns = {0, 1, 2, 7, 42, r - 1, r, r + 1}
    ns.update(samples(0, min(r**m, cap)))
    ns.add(min(r**m, cap) - 1)
    if M > m and r**m < cap:
        ns.update({r**m, r**m + 1, min(r ** (m + 1), cap) - 1})
        ns.update(samples(r**m, min(r ** (m + 1), cap)))
    if M > m + 1 and r ** (m + 1) < cap:
        ns.update({r ** (m + 1), min(r ** (m + 2), cap) - 1})
        ns.update(samples(r ** (m + 1), min(r ** (m + 2), cap)))
    ns.add(cap - 1)
    ns.update(samples(0, cap, 4))
    return sorted(n for n in ns if 0 <= n < cap)


def _encrypt_raw(codec: Dealcode, v: int, d: int) -> str:
    """Encrypt a raw stage value (bypassing range checks) to build invalid codes."""
    numerals = []
    for _ in range(d):
        v, rem = divmod(v, codec.radix)
        numerals.append(rem)
    numerals.reverse()
    ct = codec._ff1.encrypt(codec._tweak, numerals)
    chars = codec.alphabet
    return "".join(chars[x] for x in ct)


def invalid_codes_for(codec: Dealcode) -> list:
    """Codes that MUST be rejected by decode."""
    chars = codec.alphabet
    r, m, M = codec.radix, codec.min_length, codec.max_length
    bad = [chars[0] * (m - 1)]  # too short
    if M < 24:
        bad.append(chars[0] * (M + 1))  # too long
    outside = next(
        chr(c)
        for c in range(0x21, 0x7F)
        if chr(c) not in set(chars) and chr(c) not in '"\\'
    )
    bad.append(outside + chars[0] * (m - 1))
    if M > m:
        # stage-range violations at d = m+1: first and last forbidden value
        d = m + 1
        for v in (r**d - r ** (d - 1), r**d - 1):
            bad.append(_encrypt_raw(codec, v, d))
    if r**M > COUNTER_BOUND:
        # counter-bound violations at d = M
        base = 0 if M == m else r ** (M - 1)
        first_over = COUNTER_BOUND - base
        stage_cap = r**M - base
        for v in {first_over, stage_cap - 1}:
            bad.append(_encrypt_raw(codec, v, M))
    return bad


def normalize_cases_for(codec: Dealcode, name: str):
    cases = []
    probe = min(123456, codec.capacity - 1)
    if name in ("hex", "base36"):
        code = codec.encode(probe)
        if code != code.upper():
            cases.append({"input": code.upper(), "n": str(probe)})
    elif name == "base32":
        code = codec.encode(probe)
        if code != code.lower():
            cases.append({"input": code.lower(), "n": str(probe)})
    elif name == "crockford":
        code = codec.encode(probe)
        mangled = code.lower().replace("0", "o").replace("1", "i")
        cases.append({"input": mangled, "n": str(probe)})
    return cases


def main() -> None:
    out = {"spec": "dealcode/v1", "configs": []}
    for c in CONFIGS:
        alphabet = c["alphabet"]
        is_preset = alphabet in PRESET_NAMES
        codec = Dealcode(
            c["key"],
            alphabet,
            min_length=c["min_length"],
            max_length=c.get("max_length"),
            domain=c["domain"],
        )
        cfg = {
            "name": c["name"],
            "alphabet": alphabet if is_preset else "custom",
            "min_length": codec.min_length,
            "max_length": codec.max_length,
            "domain": c["domain"],
        }
        if isinstance(c["key"], str):
            cfg["key_string"] = c["key"]
        else:
            cfg["key_hex"] = c["key"].hex()
        if not is_preset:
            cfg["custom_alphabet"] = alphabet
        cfg["vectors"] = []
        for n in counters_for(codec):
            code = codec.encode(n)
            assert codec.decode(code) == n
            cfg["vectors"].append({"n": str(n), "code": code})
        cfg["invalid_codes"] = invalid_codes_for(codec)
        cfg["normalize"] = normalize_cases_for(codec, alphabet)
        out["configs"].append(cfg)

    path = ROOT / "testvectors" / "v1.json"
    path.write_text(json.dumps(out, indent=2) + "\n")
    total = sum(len(c["vectors"]) for c in out["configs"])
    invalid = sum(len(c["invalid_codes"]) for c in out["configs"])
    print(f"wrote {path} ({len(out['configs'])} configs, {total} vectors, {invalid} invalid)")


if __name__ == "__main__":
    main()
