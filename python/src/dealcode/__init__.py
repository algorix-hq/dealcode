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
from ._ff1 import FF1

__version__ = "1.0.0"

__all__ = [
    "Dealcode",
    "DealcodeError",
    "ConfigError",
    "RangeError",
    "InvalidCodeError",
    "FF1",
    "PRESETS",
    "__version__",
]
