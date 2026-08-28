"""The dealcode codec (SPEC.md format version 1)."""

from __future__ import annotations

import hashlib
from typing import Union

from ._alphabets import PRESETS, _ascii_lower, resolve
from ._ff1 import FF1

__all__ = ["Dealcode", "DealcodeError", "ConfigError", "RangeError", "InvalidCodeError"]

_COUNTER_BOUND = 2**63  # counters live in [0, min(radix**max_length, 2**63))
_CODESPACE_BOUND = 2**128  # radix**max_length must not exceed this
_TWEAK_PREFIX = "dealcode/v1/"
_KDF_PREFIX = b"dealcode/v1/kdf"


class DealcodeError(ValueError):
    """Base class for all dealcode errors."""


class ConfigError(DealcodeError):
    """Invalid codec configuration."""


class RangeError(DealcodeError):
    """Counter out of the encodable range."""


class InvalidCodeError(DealcodeError):
    """Code fails length, charset, or range validation."""


def _encode_clean_utf8(value: str, what: str) -> bytes:
    """UTF-8 encode, rejecting U+0000 and unpaired surrogates (SPEC.md §2.1)."""
    if "\x00" in value:
        raise ConfigError(f"{what} must not contain U+0000")
    try:
        return value.encode("utf-8")
    except UnicodeEncodeError as exc:
        raise ConfigError(f"{what} must be valid Unicode (no unpaired surrogates)") from exc


def _resolve_key(key: Union[bytes, bytearray, str]) -> bytes:
    """Key material handling (SPEC.md §2.1)."""
    if isinstance(key, str):
        if _ascii_lower(key) in PRESETS:
            raise ConfigError(
                f'string key "{key}" is a preset alphabet name '
                f"— did you swap the key and alphabet arguments?"
            )
        material = _encode_clean_utf8(key, "string key material")
        if not material:
            raise ConfigError("key must not be empty")
        return hashlib.sha256(_KDF_PREFIX + material).digest()
    if isinstance(key, (bytes, bytearray)):
        material = bytes(key)
        if not material:
            raise ConfigError("key must not be empty")
        if len(material) in (16, 24, 32):
            return material
        return hashlib.sha256(_KDF_PREFIX + material).digest()
    raise ConfigError("key must be bytes or a string")


def _default_max_length(radix: int, min_length: int) -> int:
    length = min_length
    cap = radix**min_length
    while cap * radix < _COUNTER_BOUND:  # largest L with radix**L <= 2**63 - 1
        cap *= radix
        length += 1
    return length


class Dealcode:
    """Bijective counter <-> code mapping. See SPEC.md.

    >>> codec = Dealcode(key="example-key")
    >>> codec.decode(codec.encode(42)) == 42
    True

    Instances are immutable, thread-safe, and cheap to keep around; create one
    per code namespace at startup and reuse it.
    """

    __slots__ = (
        "_alphabet",
        "_radix",
        "_min_length",
        "_max_length",
        "_domain",
        "_tweak",
        "_ff1",
        "_index",
        "_powers",
        "_capacity",
    )

    def __init__(
        self,
        key: Union[bytes, bytearray, str],
        alphabet: str = "hex",
        *,
        min_length: int = 6,
        max_length: Union[int, None] = None,
        domain: str = "",
    ) -> None:
        aes_key = _resolve_key(key)
        try:
            self._alphabet = resolve(alphabet)
        except ValueError as exc:
            raise ConfigError(str(exc)) from exc
        radix = len(self._alphabet.chars)

        # Bound the lengths BEFORE computing any power (SPEC.md §2): 128 is
        # the structural maximum (radix >= 2, radix**max_length <= 2^128).
        if not isinstance(min_length, int) or isinstance(min_length, bool) or not (
            2 <= min_length <= 128
        ):
            raise ConfigError("min_length must be an integer in [2, 128]")
        if radix**min_length < 100:
            raise ConfigError(
                "radix**min_length must be at least 100 (FF1 minimum domain)"
            )
        if max_length is None:
            max_length = _default_max_length(radix, min_length)
        if not isinstance(max_length, int) or isinstance(max_length, bool) or not (
            min_length <= max_length <= 128
        ):
            raise ConfigError("max_length must be an integer in [min_length, 128]")
        if radix**max_length > _CODESPACE_BOUND:
            raise ConfigError("radix**max_length must not exceed 2^128")
        if not isinstance(domain, str):
            raise ConfigError("domain must be a string")
        if len(_encode_clean_utf8(domain, "domain")) > 255:
            raise ConfigError("domain must be at most 255 UTF-8 bytes")

        self._radix = radix
        self._min_length = min_length
        self._max_length = max_length
        self._domain = domain
        self._tweak = (_TWEAK_PREFIX + domain).encode("utf-8")
        self._ff1 = FF1(aes_key, radix)
        self._index = {c: i for i, c in enumerate(self._alphabet.chars)}
        # powers[d] = radix**d for d in [0, max_length]
        self._powers = [radix**d for d in range(max_length + 1)]
        self._capacity = min(self._powers[max_length], _COUNTER_BOUND)

    # -- introspection -------------------------------------------------------

    @property
    def alphabet(self) -> str:
        return self._alphabet.chars

    @property
    def radix(self) -> int:
        return self._radix

    @property
    def min_length(self) -> int:
        return self._min_length

    @property
    def max_length(self) -> int:
        return self._max_length

    @property
    def domain(self) -> str:
        return self._domain

    @property
    def capacity(self) -> int:
        """Number of encodable counters: min(radix ** max_length, 2**63)."""
        return self._capacity

    def __repr__(self) -> str:  # keys never appear in repr/logs
        name = self._alphabet.name or f"custom({self._radix})"
        return (
            f"Dealcode(alphabet={name!r}, min_length={self._min_length}, "
            f"max_length={self._max_length}, domain={self._domain!r})"
        )

    # -- public API ------------------------------------------------------------

    def encode(self, n: int) -> str:
        """Map counter ``n`` to its code. O(1); raises RangeError if out of range."""
        if not isinstance(n, int) or isinstance(n, bool):
            raise RangeError(f"counter must be an integer, got {type(n).__name__}")
        if n < 0 or n >= self._capacity:
            raise RangeError(f"counter {n} out of range [0, {self._capacity})")
        d, base = self._stage_of(n)
        numerals = self._to_numerals(n - base, d)
        cipher = self._ff1.encrypt(self._tweak, numerals)
        chars = self._alphabet.chars
        return "".join(chars[x] for x in cipher)

    def decode(self, code: str) -> int:
        """Map a code back to its counter. Raises InvalidCodeError if not issuable."""
        if not isinstance(code, str):
            raise InvalidCodeError(f"code must be a string, got {type(code).__name__}")
        # Length gate before normalization: normalization is length-preserving,
        # so this is behaviour-identical, and it keeps rejection of oversized
        # garbage O(1) instead of normalizing megabytes first (SPEC §7).
        d = len(code)
        if d < self._min_length or d > self._max_length:
            raise InvalidCodeError(
                f"code length {d} outside [{self._min_length}, {self._max_length}]"
            )
        normalized = self._alphabet.normalize(code)
        index = self._index
        try:
            numerals = [index[c] for c in normalized]
        except KeyError as exc:
            raise InvalidCodeError(
                f"character {exc.args[0]!r} not in alphabet"
            ) from exc
        plain = self._ff1.decrypt(self._tweak, numerals)
        v = 0
        for x in plain:
            v = v * self._radix + x
        base = 0 if d == self._min_length else self._powers[d - 1]
        if d > self._min_length and v >= self._powers[d] - base:
            raise InvalidCodeError("code was not issued by this codec")
        n = base + v
        if n >= _COUNTER_BOUND:
            raise InvalidCodeError("code was not issued by this codec")
        return n

    # -- helpers ---------------------------------------------------------------

    def _stage_of(self, n: int) -> "tuple[int, int]":
        powers, m = self._powers, self._min_length
        if n < powers[m]:
            return m, 0
        d = m + 1
        while n >= powers[d]:
            d += 1
        return d, powers[d - 1]

    def _to_numerals(self, value: int, m: int) -> "list[int]":
        out = [0] * m
        r = self._radix
        for i in range(m - 1, -1, -1):
            value, out[i] = divmod(value, r)
        return out
