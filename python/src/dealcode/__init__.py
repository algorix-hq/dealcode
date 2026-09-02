"""dealcode — collision-free, random-looking codes from a counter.

See https://github.com/algorix-hq/dealcode (SPEC.md is normative).
"""

from ._alphabets import PRESETS
from ._codec import (
    ConfigError,
    Dealcode,
    DealcodeError,
    InvalidCodeError,
    RangeError,
)
from ._cycle import CyclingDealcode
from ._ff1 import FF1
from ._range import RangeDealcode

__version__ = "1.0.1"

__all__ = [
    "Dealcode",
    "CyclingDealcode",
    "RangeDealcode",
    "DealcodeError",
    "ConfigError",
    "RangeError",
    "InvalidCodeError",
    "FF1",
    "PRESETS",
    "__version__",
]
