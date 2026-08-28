"""Fixed-length cycling mode (SPEC.md §11, tweak namespace ``dealcode/v1c/``)."""

from __future__ import annotations

from typing import Union

from ._alphabets import resolve
from ._codec import (
    _COUNTER_BOUND,
    ConfigError,
    InvalidCodeError,
    RangeError,
    _encode_clean_utf8,
    _resolve_key,
)
from ._ff1 import FF1

__all__ = ["CyclingDealcode"]

_CYCLE_TWEAK_PREFIX = "dealcode/v1c/"


class CyclingDealcode:
    """Fixed-length codes that refill the same space cycle after cycle.

    Codes are always exactly ``length`` characters. Counters ``n`` map to
    cycle ``n // capacity`` and in-cycle value ``n % capacity``; every cycle
    is a different permutation of the same code space (a different FF1
    tweak), so when the space is exhausted it refills in a new order instead
    of growing.

    Codes REPEAT across cycles by design — keep at most one cycle's codes
    live per uniqueness scope (``UNIQUE(cycle, code)``, not
    ``UNIQUE(code)``), and store which cycle a live code belongs to:
    :meth:`decode` needs it.

    >>> codec = CyclingDealcode(key="example-key", alphabet="crockford", length=6)
    >>> n = 3 * codec.capacity + 7        # cycle 3, value 7
    >>> codec.decode(codec.encode(n), cycle=3) == n
    True

    Instances are immutable and safe for concurrent use.
    """

    __slots__ = (
        "_alphabet",
        "_radix",
        "_length",
        "_domain",
        "_ff1",
        "_index",
        "_capacity",
        "_max_cycle",
    )

    def __init__(
        self,
        key: Union[bytes, bytearray, str],
        alphabet: str = "hex",
        *,
        length: int = 6,
        domain: str = "",
    ) -> None:
        aes_key = _resolve_key(key)
        try:
            self._alphabet = resolve(alphabet)
        except ValueError as exc:
            raise ConfigError(str(exc)) from exc
        radix = len(self._alphabet.chars)

        # Bound the length BEFORE computing any power (SPEC §11.1).
        if not isinstance(length, int) or isinstance(length, bool) or not (
            2 <= length <= 128
        ):
            raise ConfigError("length must be an integer in [2, 128]")
        if radix**length < 100:
            raise ConfigError(
                "radix**length must be at least 100 (FF1 minimum domain)"
            )
        if radix**length > _COUNTER_BOUND:
            raise ConfigError(
                "radix**length must not exceed 2^63 in cycling mode — a cycle "
                "must be completable; use Dealcode with min_length == "
                "max_length for larger fixed spaces"
            )
        if not isinstance(domain, str):
            raise ConfigError("domain must be a string")
        if len(_encode_clean_utf8(domain, "domain")) > 255:
            raise ConfigError("domain must be at most 255 UTF-8 bytes")

        self._radix = radix
        self._length = length
        self._domain = domain
        self._ff1 = FF1(aes_key, radix)
        self._index = {c: i for i, c in enumerate(self._alphabet.chars)}
        self._capacity = radix**length
        self._max_cycle = (_COUNTER_BOUND - 1) // self._capacity

    # -- introspection -------------------------------------------------------

    @property
    def alphabet(self) -> str:
        return self._alphabet.chars

    @property
    def radix(self) -> int:
        return self._radix

    @property
    def length(self) -> int:
        return self._length

    @property
    def domain(self) -> str:
        return self._domain

    @property
    def capacity(self) -> int:
        """Codes per cycle: radix ** length."""
        return self._capacity

    @property
    def max_cycle(self) -> int:
        """Largest usable cycle: (2**63 - 1) // capacity."""
        return self._max_cycle

    def cycle_of(self, n: int) -> int:
        """The cycle that counter ``n`` belongs to (``n // capacity``)."""
        if not isinstance(n, int) or isinstance(n, bool) or n < 0 or n >= _COUNTER_BOUND:
            raise RangeError(f"counter {n!r} out of range [0, {_COUNTER_BOUND})")
        return n // self._capacity

    def __repr__(self) -> str:  # keys never appear in repr/logs
        name = self._alphabet.name or f"custom({self._radix})"
        return (
            f"CyclingDealcode(alphabet={name!r}, length={self._length}, "
            f"domain={self._domain!r})"
        )

    # -- public API ------------------------------------------------------------

    def encode(self, n: int) -> str:
        """Map counter ``n`` to its fixed-length code (cycle = n // capacity)."""
        if not isinstance(n, int) or isinstance(n, bool):
            raise RangeError(f"counter must be an integer, got {type(n).__name__}")
        if n < 0 or n >= _COUNTER_BOUND:
            raise RangeError(f"counter {n} out of range [0, {_COUNTER_BOUND})")
        cycle, v = divmod(n, self._capacity)
        numerals = self._to_numerals(v)
        cipher = self._ff1.encrypt(self._tweak_for(cycle), numerals)
        chars = self._alphabet.chars
        return "".join(chars[x] for x in cipher)

    def decode(self, code: str, cycle: int) -> int:
        """Map ``code`` issued in ``cycle`` back to its counter.

        The cycle is required: the same string recurs in every cycle, mapping
        to a different counter each time.
        """
        if not isinstance(cycle, int) or isinstance(cycle, bool) or not (
            0 <= cycle <= self._max_cycle
        ):
            raise RangeError(f"cycle {cycle!r} out of range [0, {self._max_cycle}]")
        if not isinstance(code, str):
            raise InvalidCodeError(f"code must be a string, got {type(code).__name__}")
        # Length gate before normalization (SPEC §7 via §11.2).
        if len(code) != self._length:
            raise InvalidCodeError(
                f"code length {len(code)} != {self._length} (fixed-length mode)"
            )
        normalized = self._alphabet.normalize(code)
        index = self._index
        try:
            numerals = [index[c] for c in normalized]
        except KeyError as exc:
            raise InvalidCodeError(
                f"character {exc.args[0]!r} not in alphabet"
            ) from exc
        plain = self._ff1.decrypt(self._tweak_for(cycle), numerals)
        v = 0
        for x in plain:
            v = v * self._radix + x
        n = cycle * self._capacity + v
        if n >= _COUNTER_BOUND:  # only reachable in the final partial cycle
            raise InvalidCodeError("code was not issued in this cycle")
        return n

    # -- helpers ---------------------------------------------------------------

    def _tweak_for(self, cycle: int) -> bytes:
        return f"{_CYCLE_TWEAK_PREFIX}{cycle}/{self._domain}".encode("utf-8")

    def _to_numerals(self, value: int) -> "list[int]":
        out = [0] * self._length
        r = self._radix
        for i in range(self._length - 1, -1, -1):
            value, out[i] = divmod(value, r)
        return out
