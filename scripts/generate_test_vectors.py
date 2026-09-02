#!/usr/bin/env python3
"""Generate testvectors/v1.json from the Python reference implementation.

Run from the repository root:  python3 scripts/generate_test_vectors.py
"""

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python" / "src"))

from dealcode import (  # noqa: E402
    ConfigError,
    CyclingDealcode,
    Dealcode,
    InvalidCodeError,
    RangeDealcode,
    RangeError,
)

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
    # --- coverage extensions (append-only; earlier configs must stay put) ---
    dict(name="base36", key=KEY128, alphabet="base36", min_length=6, domain=""),
    # smallest legal FF1 domain: r^min_length == 100 exactly
    dict(name="dec-min2", key=KEY128, alphabet="dec", min_length=2, domain=""),
    # code space of exactly 2^128: exercises the 128-bit arithmetic path
    dict(name="hex-max32", key=KEY256, alphabet="hex", min_length=6, max_length=32, domain=""),
    # code space of exactly 2^63: max_counter == 2^63 - 1 with no headroom
    dict(name="octal-2pow63", key=KEY128, alphabet="01234567", min_length=21, max_length=21, domain=""),
    # radix extremes: 2 (smallest) and 94 (every printable ASCII char)
    dict(name="binary", key=KEY128, alphabet="01", min_length=7, domain=""),
    dict(name="printable94", key=KEY256, alphabet="".join(chr(c) for c in range(0x21, 0x7F)), min_length=2, domain=""),
    # a hex-looking STRING key must be KDF'd as UTF-8, never hex-decoded:
    # these two configs must NOT produce the same codes
    dict(name="hexlike-string-key", key="deadbeefdeadbeefdeadbeefdeadbeef", alphabet="hex", min_length=6, domain=""),
    dict(name="hexlike-bytes-key", key=bytes.fromhex("deadbeefdeadbeefdeadbeefdeadbeef"), alphabet="hex", min_length=6, domain=""),
    # multi-byte UTF-8 in key material and tweak (domain)
    dict(name="korean-string-key", key="딜코드 비밀 키 🔑", alphabet="hex", min_length=6, domain=""),
    dict(name="korean-domain", key=KEY128, alphabet="hex", min_length=6, domain="주문·쿠폰"),
    # domain at the 255-UTF-8-byte limit (85 three-byte characters)
    dict(name="domain-255b", key=KEY128, alphabet="hex", min_length=6, domain="가" * 85),
]

# Constructions every implementation must reject with its ConfigError kind.
INVALID_CONFIGS = [
    dict(name="empty-key", key_string="", alphabet="hex", min_length=6),
    dict(name="duplicate-alphabet-chars", key_hex=KEY128.hex(), custom_alphabet="abcabc", min_length=6),
    dict(name="codespace-under-100", key_hex=KEY128.hex(), custom_alphabet="abcdefghi", min_length=2),  # 9^2 = 81 < 100
    dict(name="min-length-one", key_hex=KEY128.hex(), alphabet="hex", min_length=1),
    dict(name="min-above-max", key_hex=KEY128.hex(), alphabet="hex", min_length=8, max_length=6),
    dict(name="domain-256-bytes", key_hex=KEY128.hex(), alphabet="hex", min_length=6, domain="x" * 256),
    dict(name="alphabet-with-space", key_hex=KEY128.hex(), custom_alphabet="abc def", min_length=4),
    dict(name="alphabet-non-ascii", key_hex=KEY128.hex(), custom_alphabet="abcdé", min_length=4),
    dict(name="alphabet-one-char", key_hex=KEY128.hex(), custom_alphabet="a", min_length=8),
    dict(name="codespace-over-2pow128", key_hex=KEY128.hex(), alphabet="hex", min_length=6, max_length=33),
    # guard: custom alphabet that case-insensitively matches a preset name
    dict(name="preset-lookalike-alphabet", key_hex=KEY128.hex(), custom_alphabet="HEX", min_length=6),
    # guard: string key that equals a preset name (swapped arguments)
    dict(name="preset-name-as-key", key_string="crockford", alphabet="hex", min_length=6),
]

# Counters every implementation must reject with its RangeError kind (as
# strings; a language whose counter type cannot even represent the value may
# treat unrepresentability as the rejection).
RANGE_COUNTERS = ["-1", str(COUNTER_BOUND), str(2**64)]

# --- fixed-length cycling mode (SPEC §11, testvectors/v1c.json) -------------

CYCLE_CONFIGS = [
    dict(name="crockford-pnr6", key=KEY128, alphabet="crockford", length=6, domain=""),
    dict(name="dec-6", key=KEY256, alphabet="dec", length=6, domain="bookings"),
    dict(name="hex-4-small", key=KEY192, alphabet="hex", length=4, domain=""),
    dict(name="base62-8", key=KEY128, alphabet="base62", length=8, domain=""),
    dict(name="dec-2-many-cycles", key=KEY_STR, alphabet="dec", length=2, domain=""),
    # capacity exactly 2^63: max_cycle == 0, a single never-completed cycle
    dict(name="octal-21-single-cycle", key=KEY128, alphabet="01234567", length=21, domain=""),
    dict(name="korean-domain", key=KEY128, alphabet="hex", length=6, domain="예약·코드"),
    # domain at the 255-UTF-8-byte limit (85 three-byte characters)
    dict(name="domain-255b", key=KEY128, alphabet="hex", length=6, domain="가" * 85),
    # radix extreme: every printable ASCII char
    dict(name="printable94-3", key=KEY256, alphabet="".join(chr(c) for c in range(0x21, 0x7F)), length=3, domain=""),
    # hex-looking STRING key is KDF'd, never hex-decoded: these two differ
    dict(name="hexlike-string-key", key="deadbeefdeadbeefdeadbeefdeadbeef", alphabet="hex", length=6, domain=""),
    dict(name="hexlike-bytes-key", key=bytes.fromhex("deadbeefdeadbeefdeadbeefdeadbeef"), alphabet="hex", length=6, domain=""),
]

CYCLE_INVALID_CONFIGS = [
    dict(name="length-one", key_hex=KEY128.hex(), alphabet="dec", length=1),
    dict(name="length-129", key_hex=KEY128.hex(), alphabet="hex", length=129),
    dict(name="codespace-under-100", key_hex=KEY128.hex(), custom_alphabet="abcdefghi", length=2),
    dict(name="capacity-over-2pow63", key_hex=KEY128.hex(), alphabet="hex", length=16),
    dict(name="preset-lookalike-alphabet", key_hex=KEY128.hex(), custom_alphabet="HEX", length=6),
    dict(name="preset-name-as-key", key_string="crockford", alphabet="hex", length=6),
    dict(name="empty-key", key_string="", alphabet="hex", length=6),
    dict(name="domain-256-bytes", key_hex=KEY128.hex(), alphabet="hex", length=6, domain="x" * 256),
]


def cycle_counters_for(codec):
    cap, top = codec.capacity, COUNTER_BOUND
    ns = {0, 1, 2, 7, 42, cap - 1}
    if codec.max_cycle >= 2:
        ns.update({cap, cap + 1, 2 * cap - 1, 2 * cap, 2 * cap + 7})
    if codec.max_cycle >= 12345:
        ns.update(12345 * cap + s for s in (0, 1, cap - 1))
    # the IEEE-double / JS Number.MAX_SAFE_INTEGER seam
    ns.update({2**53 - 1, 2**53, 2**53 + 1})
    ns.add(top - 1)  # final (possibly partial) cycle
    ns.update(samples(0, min(3 * cap, top), 4))
    ns.update(samples(0, top, 3))
    return sorted(n for n in ns if 0 <= n < top)


def _cycle_encrypt_raw(codec, cycle: int, v: int) -> str:
    numerals = codec._to_numerals(v)
    ct = codec._ff1.encrypt(codec._tweak_for(cycle), numerals)
    chars = codec.alphabet
    return "".join(chars[x] for x in ct)


def cycle_invalid_codes_for(codec) -> list:
    chars, L = codec.alphabet, codec.length
    bad = [
        {"cycle": "0", "code": chars[0] * (L - 1)},   # too short
        {"cycle": "0", "code": chars[0] * (L + 1)},   # too long
        {"cycle": "0", "code": ""},
        {"cycle": "0", "code": " " + chars[0] * (L - 1)},
        {"cycle": "0", "code": chars[0] * (L - 1) + "\n"},
    ]
    outside = next(
        (
            chr(c)
            for c in range(0x21, 0x7F)
            if chr(c) not in set(chars) and chr(c) not in '"\\'
        ),
        None,
    )
    if outside is not None:
        bad.append({"cycle": "0", "code": outside + chars[0] * (L - 1)})
    # a code whose counter would exceed 2^63 in the final partial cycle
    e_last = codec.max_cycle
    first_over = COUNTER_BOUND - e_last * codec.capacity
    if first_over < codec.capacity:
        for v in {first_over, codec.capacity - 1}:
            bad.append({"cycle": str(e_last), "code": _cycle_encrypt_raw(codec, e_last, v)})
    for entry in bad:
        try:
            codec.decode(entry["code"], int(entry["cycle"]))
        except InvalidCodeError:
            pass
        else:
            raise AssertionError(f"invalid cycle code accepted: {entry}")
    return bad


def cycle_normalize_for(codec, name: str):
    cases = []
    probe = 123456 % codec.capacity
    if name in ("hex", "base36"):
        code = codec.encode(probe)
        if code != code.upper():
            cases.append({"cycle": "0", "input": code.upper(), "n": str(probe)})
    elif name == "crockford":
        code = codec.encode(probe)
        cases.append(
            {"cycle": "0", "input": code.lower().replace("0", "o").replace("1", "i"), "n": str(probe)}
        )
    return cases


def generate_v1c() -> dict:
    out = {"spec": "dealcode/v1c", "configs": []}
    for c in CYCLE_CONFIGS:
        alphabet = c["alphabet"]
        is_preset = alphabet in PRESET_NAMES
        codec = CyclingDealcode(c["key"], alphabet, length=c["length"], domain=c["domain"])
        cfg = {
            "name": c["name"],
            "alphabet": alphabet if is_preset else "custom",
            "length": codec.length,
            "domain": c["domain"],
            "capacity": str(codec.capacity),
            "max_cycle": str(codec.max_cycle),
        }
        if isinstance(c["key"], str):
            cfg["key_string"] = c["key"]
        else:
            cfg["key_hex"] = c["key"].hex()
        if not is_preset:
            cfg["custom_alphabet"] = alphabet
        cfg["vectors"] = []
        for n in cycle_counters_for(codec):
            code = codec.encode(n)
            assert codec.decode(code, n // codec.capacity) == n
            cfg["vectors"].append({"n": str(n), "code": code})
        cfg["invalid_codes"] = cycle_invalid_codes_for(codec)
        cfg["normalize"] = cycle_normalize_for(codec, alphabet)
        cfg["range_counters"] = list(dict.fromkeys(RANGE_COUNTERS))
        for c_str in cfg["range_counters"]:
            try:
                codec.encode(int(c_str))
            except RangeError:
                pass
            else:
                raise AssertionError(f"cycle range counter accepted: {c_str}")
        cfg["invalid_cycles"] = ["-1", str(codec.max_cycle + 1)]
        probe = codec.encode(0)
        for cyc in cfg["invalid_cycles"]:
            try:
                codec.decode(probe, int(cyc))
            except RangeError:
                pass
            else:
                raise AssertionError(f"invalid cycle accepted: {cyc}")
        out["configs"].append(cfg)

    out["invalid_configs"] = []
    for ic in CYCLE_INVALID_CONFIGS:
        key = bytes.fromhex(ic["key_hex"]) if "key_hex" in ic else ic["key_string"]
        alphabet = ic.get("custom_alphabet", ic.get("alphabet"))
        try:
            CyclingDealcode(key, alphabet, length=ic["length"], domain=ic.get("domain", ""))
        except ConfigError:
            pass
        else:
            raise AssertionError(f"invalid cycle config accepted: {ic['name']}")
        out["invalid_configs"].append(dict(ic))
    return out


# --- integer range mode (SPEC §12, testvectors/v1r.json) --------------------

RANGE_MODE_CONFIGS = [
    # the motivating shape: 6-digit codes with no leading zero (96^3 = 884736)
    dict(name="no-leading-zero-6", key=KEY128, low=100_000, high=999_999, domain=""),
    dict(name="bookings-domain", key=KEY256, low=100_000, high=999_999, domain="bookings"),
    # N is an admissible power: capacity == N exactly (100^3)
    dict(name="full-million", key=KEY128, low=0, high=999_999, domain=""),
    # smallest legal span (10^2 = 100, no dead zone)
    dict(name="min-span-100", key=KEY192, low=0, high=99, domain=""),
    # small span parked at the very top of the 2^63 space
    dict(name="min-span-high-offset", key=KEY128, low=2**63 - 101, high=2**63 - 1, domain=""),
    # the whole counter space: N = 2^63 = 128^9, capacity == N
    dict(name="full-counter-space", key=KEY128, low=0, high=2**63 - 1, domain=""),
    # capacity tie (65536 = 256^2 = 16^4 = 4^8 = 2^16): smallest m must win
    dict(name="tie-smallest-m", key=KEY128, low=1_000, high=66_535, domain=""),
    # 5-digit no-leading-zero: N=90000 -> 44^3 = 85184
    dict(name="five-digit-string-key", key=KEY_STR, low=10_000, high=99_999, domain=""),
    dict(name="korean-domain", key=KEY128, low=100_000, high=999_999, domain="예약·코드"),
    # domain at the 255-UTF-8-byte limit (85 three-byte characters)
    dict(name="domain-255b", key=KEY128, low=100_000, high=999_999, domain="가" * 85),
    # hex-looking STRING key is KDF'd, never hex-decoded: these two differ
    dict(name="hexlike-string-key", key="deadbeefdeadbeefdeadbeefdeadbeef", low=100_000, high=999_999, domain=""),
    dict(name="hexlike-bytes-key", key=bytes.fromhex("deadbeefdeadbeefdeadbeefdeadbeef"), low=100_000, high=999_999, domain=""),
]

RANGE_MODE_INVALID_CONFIGS = [
    dict(name="span-under-100", key_hex=KEY128.hex(), low="0", high="98"),
    dict(name="low-above-high", key_hex=KEY128.hex(), low="10", high="9"),
    dict(name="negative-low", key_hex=KEY128.hex(), low="-1", high="200"),
    dict(name="high-at-2pow63", key_hex=KEY128.hex(), low="0", high=str(2**63)),
    dict(name="preset-name-as-key", key_string="crockford", low="100000", high="999999"),
    dict(name="empty-key", key_string="", low="100000", high="999999"),
    dict(name="domain-256-bytes", key_hex=KEY128.hex(), low="100000", high="999999", domain="x" * 256),
]


def range_mode_counters_for(codec) -> list:
    cap = codec.capacity
    ns = {0, 1, 2, 7, 42, cap // 2, cap - 2, cap - 1}
    # the IEEE-double / JS Number.MAX_SAFE_INTEGER seam, where it exists
    ns.update(s for s in (2**53 - 1, 2**53, 2**53 + 1) if s < cap)
    ns.update(samples(0, cap, 4))
    return sorted(n for n in ns if 0 <= n < cap)


def range_mode_invalid_codes_for(codec) -> list:
    top = codec.low + codec.capacity  # first dead-zone value (if any)
    bad = {codec.high + 1, 2**63, 2**64}
    if codec.low > 0:
        bad.update({codec.low - 1, 0})
    if top <= codec.high:
        bad.update({top, codec.high})  # both ends of the dead zone
    out = sorted(bad)
    for c in out:
        try:
            codec.decode(c)
        except InvalidCodeError:
            pass
        else:
            raise AssertionError(f"invalid range code accepted: {c}")
    return [str(c) for c in out]


def generate_v1r() -> dict:
    out = {"spec": "dealcode/v1r", "configs": []}
    for c in RANGE_MODE_CONFIGS:
        codec = RangeDealcode(c["key"], low=c["low"], high=c["high"], domain=c["domain"])
        cfg = {
            "name": c["name"],
            "low": str(c["low"]),
            "high": str(c["high"]),
            "domain": c["domain"],
            "radix": codec.radix,
            "m": codec._m,
            "capacity": str(codec.capacity),
        }
        if isinstance(c["key"], str):
            cfg["key_string"] = c["key"]
        else:
            cfg["key_hex"] = c["key"].hex()
        cfg["vectors"] = []
        for n in range_mode_counters_for(codec):
            code = codec.encode(n)
            assert codec.low <= code < codec.low + codec.capacity
            assert codec.decode(code) == n
            cfg["vectors"].append({"n": str(n), "code": str(code)})
        cfg["invalid_codes"] = range_mode_invalid_codes_for(codec)
        cfg["range_counters"] = ["-1", str(codec.capacity), str(2**63), str(2**64)]
        for c_str in cfg["range_counters"]:
            try:
                codec.encode(int(c_str))
            except RangeError:
                pass
            else:
                raise AssertionError(f"range-mode counter accepted: {c_str}")
        out["configs"].append(cfg)

    out["invalid_configs"] = []
    for ic in RANGE_MODE_INVALID_CONFIGS:
        key = bytes.fromhex(ic["key_hex"]) if "key_hex" in ic else ic["key_string"]
        try:
            RangeDealcode(
                key,
                low=int(ic["low"]),
                high=int(ic["high"]),
                domain=ic.get("domain", ""),
            )
        except ConfigError:
            pass
        else:
            raise AssertionError(f"invalid range config accepted: {ic['name']}")
        out["invalid_configs"].append(dict(ic))
    return out


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
    # exact first/last counter of every remaining stage
    for d in range(m + 3, M + 1):
        lo = r ** (d - 1)
        if lo >= cap:
            break
        ns.update({lo - 1, lo, min(r**d, cap) - 1})
    # the IEEE-double / JS Number.MAX_SAFE_INTEGER seam
    ns.update({2**53 - 1, 2**53, 2**53 + 1})
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
    if M < 130:
        bad.append(chars[0] * (M + 1))  # too long
    # some character outside the alphabet (absent for printable94, where
    # every printable ASCII char is inside)
    outside = next(
        (
            chr(c)
            for c in range(0x21, 0x7F)
            if chr(c) not in set(chars) and chr(c) not in '"\\'
        ),
        None,
    )
    if outside is not None:
        bad.append(outside + chars[0] * (m - 1))
    # decode must never trim or Unicode-normalize
    bad.append("")
    bad.append(" " + chars[0] * (m - 1))
    bad.append(chars[0] * (m - 1) + "\n")
    bad.append("ｅ" * m)  # fullwidth lookalikes
    if M > m:
        # stage-range violations at d = m+1: first and last forbidden value
        d = m + 1
        for v in (r**d - r ** (d - 1), r**d - 1):
            bad.append(_encrypt_raw(codec, v, d))
    if r**M > COUNTER_BOUND:
        # counter-bound violations, aimed at the stages where they can occur:
        # the stage whose value range straddles 2^63, the first fully
        # unreachable stage, and the last stage.
        def stage_base(d: int) -> int:
            return 0 if d == m else r ** (d - 1)

        targets = []
        crossing = [d for d in range(m, M + 1) if stage_base(d) < COUNTER_BOUND < r**d]
        if crossing:
            d = crossing[0]
            first_over = COUNTER_BOUND - stage_base(d)
            targets.append((d, first_over))  # decodes to exactly 2^63
            targets.append((d, r**d - stage_base(d) - 1))
        unreachable = [d for d in range(m, M + 1) if stage_base(d) >= COUNTER_BOUND]
        for d in {unreachable[0], M} if unreachable else set():
            targets.append((d, 0))
            targets.append((d, r**d - stage_base(d) - 1))
        for d, v in dict.fromkeys(targets):
            bad.append(_encrypt_raw(codec, v, d))
    for code in bad:
        try:
            codec.decode(code)
        except InvalidCodeError:
            pass
        else:
            raise AssertionError(f"invalid code accepted: {code!r}")
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
        # pin the O->0 and L/l->1 mappings too: find a nearby code that
        # actually contains a 0 or 1
        for n in range(probe, probe + 4096):
            code = codec.encode(n)
            if "0" in code or "1" in code:
                cases.append({"input": code.replace("0", "O").replace("1", "L"), "n": str(n)})
                cases.append({"input": code.replace("1", "l"), "n": str(n)})
                break
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
        counters = ["-1", str(codec.capacity)] + RANGE_COUNTERS
        cfg["range_counters"] = list(dict.fromkeys(counters))
        for c_str in cfg["range_counters"]:
            try:
                codec.encode(int(c_str))
            except RangeError:
                pass
            else:
                raise AssertionError(f"range counter accepted: {c_str}")
        out["configs"].append(cfg)

    out["invalid_configs"] = []
    for ic in INVALID_CONFIGS:
        entry = {k: v for k, v in ic.items()}
        kwargs = dict(
            min_length=ic.get("min_length"),
            max_length=ic.get("max_length"),
            domain=ic.get("domain", ""),
        )
        key = bytes.fromhex(ic["key_hex"]) if "key_hex" in ic else ic["key_string"]
        alphabet = ic.get("custom_alphabet", ic.get("alphabet"))
        try:
            Dealcode(key, alphabet, **{k: v for k, v in kwargs.items() if v is not None})
        except ConfigError:
            pass
        else:
            raise AssertionError(f"invalid config accepted: {ic['name']}")
        out["invalid_configs"].append(entry)

    path = ROOT / "testvectors" / "v1.json"
    path.write_text(json.dumps(out, indent=2) + "\n")
    total = sum(len(c["vectors"]) for c in out["configs"])
    invalid = sum(len(c["invalid_codes"]) for c in out["configs"])
    print(f"wrote {path} ({len(out['configs'])} configs, {total} vectors, {invalid} invalid)")

    v1c = generate_v1c()
    path_c = ROOT / "testvectors" / "v1c.json"
    path_c.write_text(json.dumps(v1c, indent=2) + "\n")
    total_c = sum(len(c["vectors"]) for c in v1c["configs"])
    invalid_c = sum(len(c["invalid_codes"]) for c in v1c["configs"])
    print(
        f"wrote {path_c} ({len(v1c['configs'])} configs, {total_c} vectors, "
        f"{invalid_c} invalid)"
    )

    v1r = generate_v1r()
    path_r = ROOT / "testvectors" / "v1r.json"
    path_r.write_text(json.dumps(v1r, indent=2) + "\n")
    total_r = sum(len(c["vectors"]) for c in v1r["configs"])
    invalid_r = sum(len(c["invalid_codes"]) for c in v1r["configs"])
    print(
        f"wrote {path_r} ({len(v1r['configs'])} configs, {total_r} vectors, "
        f"{invalid_r} invalid)"
    )


if __name__ == "__main__":
    main()
