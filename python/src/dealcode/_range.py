"""Integer range mode (SPEC.md §12, tweak namespace ``dealcode/v1r/``)."""

from __future__ import annotations

from typing import Tuple, Union

from ._codec import (
    ConfigError,
    InvalidCodeError,
    RangeError,
    _encode_clean_utf8,
    _resolve_key,
)
from ._ff1 import FF1

__all__ = ["RangeDealcode"]

_RANGE_TWEAK_PREFIX = "dealcode/v1r/"
_BOUND = 2**63  # low/high live in [0, 2^63)
_MAX_RADIX = 256  # numerals stay one byte in every FF1 core (SPEC §12.2)


def _iroot(n: int, m: int) -> int:
    """Largest integer r with r**m <= n (exact integer arithmetic)."""
    if n < 1:
        return 0
    lo, hi = 1, 1
    while hi**m <= n:
        hi *= 2
    while hi - lo > 1:
        mid = (lo + hi) // 2
        if mid**m <= n:
            lo = mid
        else:
            hi = mid
    return lo


def _select_domain(n: int) -> Tuple[int, int, int]:
    """SPEC §12.2: (radix, m, capacity) — largest radix**m <= n, smallest m on ties."""
    best_capacity = 0
    best = (0, 0)
    for m in range(2, 64):
        r = min(_iroot(n, m), _MAX_RADIX)
        if r < 2:
            continue
        c = r**m
        if c > best_capacity:  # strict '>' keeps the smallest m on ties
            best_capacity = c
            best = (r, m)
    return best[0], best[1], best_capacity


class RangeDealcode:
    """Integer codes drawn without repetition from ``[low, high]``.

    Counters ``0 <= n < capacity`` map bijectively to integer codes in
    ``[low, low + capacity - 1]`` through a single FF1 call — no loops, no
    cycle-walking. ``capacity`` is the largest FF1 domain (``radix**m`` with
    ``radix <= 256``) that fits in the range, so it can be slightly smaller
    than ``high - low + 1``; the uncovered top slice is never issued and is
    rejected by :meth:`decode`.

    Built for ranges like 100000-999999: every code is a 6-digit integer
    with no leading zero, safe to store in an integer column.

    >>> codec = RangeDealcode(key="example-key", low=100_000, high=999_999)
    >>> codec.capacity            # 96**3 — 98.3% of the 900,000-value range
    884736
    >>> code = codec.encode(0)    # an int in [100000, 984735]
    >>> codec.decode(code)
    0

    Instances are immutable and safe for concurrent use.
    """

    __slots__ = (
        "_low",
        "_high",
        "_domain",
        "_radix",
        "_m",
        "_capacity",
        "_ff1",
        "_tweak",
    )

    def __init__(
        self,
        key: Union[bytes, bytearray, str],
        *,
        low: int,
        high: int,
        domain: str = "",
    ) -> None:
        aes_key = _resolve_key(key)
        for name, value in (("low", low), ("high", high)):
            if not isinstance(value, int) or isinstance(value, bool):
                raise ConfigError(f"{name} must be an integer")
        if not (0 <= low <= high <= _BOUND - 1):
            raise ConfigError("low/high must satisfy 0 <= low <= high <= 2^63 - 1")
        if high - low + 1 < 100:
            raise ConfigError(
                "range must span at least 100 values (FF1 minimum domain)"
            )
        if not isinstance(domain, str):
            raise ConfigError("domain must be a string")
        if len(_encode_clean_utf8(domain, "domain")) > 255:
            raise ConfigError("domain must be at most 255 UTF-8 bytes")

        self._low = low
        self._high = high
        self._domain = domain
        self._radix, self._m, self._capacity = _select_domain(high - low + 1)
        self._ff1 = FF1(aes_key, self._radix)
        self._tweak = f"{_RANGE_TWEAK_PREFIX}{low}/{high}/{domain}".encode("utf-8")

    # -- introspection -------------------------------------------------------

    @property
    def low(self) -> int:
        return self._low

    @property
    def high(self) -> int:
        return self._high

    @property
    def domain(self) -> str:
        return self._domain

    @property
    def radix(self) -> int:
        """Internal FF1 radix (SPEC §12.2); informational."""
        return self._radix

    @property
    def capacity(self) -> int:
        """Number of issuable codes: the largest radix**m <= high - low + 1."""
        return self._capacity

    def __repr__(self) -> str:  # keys never appear in repr/logs
        return (
            f"RangeDealcode(low={self._low}, high={self._high}, "
            f"domain={self._domain!r})"
        )

    # -- public API ----------------------------------------------------------

    def encode(self, n: int) -> int:
        """Map counter ``n`` to its integer code in ``[low, low + capacity)``."""
        if not isinstance(n, int) or isinstance(n, bool):
            raise RangeError(f"counter must be an integer, got {type(n).__name__}")
        if n < 0 or n >= self._capacity:
            raise RangeError(f"counter {n} out of range [0, {self._capacity})")
        cipher = self._ff1.encrypt(self._tweak, self._to_numerals(n))
        v = 0
        for x in cipher:
            v = v * self._radix + x
        return self._low + v

    def decode(self, code: int) -> int:
        """Map integer ``code`` back to its counter."""
        if not isinstance(code, int) or isinstance(code, bool):
            raise InvalidCodeError(
                f"code must be an integer, got {type(code).__name__}"
            )
        if code < self._low or code > self._high:
            raise InvalidCodeError(
                f"code {code} outside range [{self._low}, {self._high}]"
            )
        v = code - self._low
        if v >= self._capacity:
            raise InvalidCodeError(
                f"code {code} in the unissued top slice of the range "
                f"(capacity {self._capacity})"
            )
        plain = self._ff1.decrypt(self._tweak, self._to_numerals(v))
        n = 0
        for x in plain:
            n = n * self._radix + x
        return n

    # -- helpers --------------------------------------------------------------

    def _to_numerals(self, value: int) -> "list[int]":
        out = [0] * self._m
        r = self._radix
        for i in range(self._m - 1, -1, -1):
            value, out[i] = divmod(value, r)
        return out
