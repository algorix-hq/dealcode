import json
from pathlib import Path

import pytest

VECTOR_DIR = Path(__file__).resolve().parents[2] / "testvectors"


@pytest.fixture(scope="session")
def nist_vectors():
    return json.loads((VECTOR_DIR / "ff1_nist.json").read_text())["vectors"]


@pytest.fixture(scope="session")
def v1_vectors():
    return json.loads((VECTOR_DIR / "v1.json").read_text())
