"""Alphabet presets and normalization rules (SPEC.md §3)."""

from __future__ import annotations

from typing import Callable, NamedTuple, Optional


class Alphabet(NamedTuple):
    name: Optional[str]  # preset name, or None for custom
    chars: str
    normalize: Callable[[str], str]


def _identity(s: str) -> str:
    return s


def _ascii_lower(s: str) -> str:
    return "".join(chr(ord(c) + 32) if "A" <= c <= "Z" else c for c in s)


def _ascii_upper(s: str) -> str:
    return "".join(chr(ord(c) - 32) if "a" <= c <= "z" else c for c in s)


def _crockford_normalize(s: str) -> str:
    s = _ascii_upper(s)
    return s.replace("O", "0").replace("I", "1").replace("L", "1")


PRESETS: dict[str, Alphabet] = {
    "dec": Alphabet("dec", "0123456789", _identity),
    "hex": Alphabet("hex", "0123456789abcdef", _ascii_lower),
    "base32": Alphabet("base32", "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567", _ascii_upper),
    "crockford": Alphabet(
        "crockford", "0123456789ABCDEFGHJKMNPQRSTVWXYZ", _crockford_normalize
    ),
    "base36": Alphabet("base36", "0123456789abcdefghijklmnopqrstuvwxyz", _ascii_lower),
    "base58": Alphabet(
        "base58",
        "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz",
        _identity,
    ),
    "base62": Alphabet(
        "base62",
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz",
        _identity,
    ),
    "base64url": Alphabet(
        "base64url",
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_",
        _identity,
    ),
}


def resolve(alphabet: str) -> Alphabet:
    """Resolve a preset name or custom alphabet string (SPEC.md §3.2)."""
    if alphabet in PRESETS:
        return PRESETS[alphabet]
    if not isinstance(alphabet, str) or not (2 <= len(alphabet) <= 94):
        raise ValueError("custom alphabet must be a string of 2-94 characters")
    if len(set(alphabet)) != len(alphabet):
        raise ValueError("custom alphabet characters must be distinct")
    if any(not (0x21 <= ord(c) <= 0x7E) for c in alphabet):
        raise ValueError("custom alphabet must be printable ASCII (0x21-0x7E)")
    return Alphabet(None, alphabet, _identity)
