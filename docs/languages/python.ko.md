# Python

[스펙](../spec.ko.md)의 레퍼런스 구현 — 공유 테스트 벡터가 이 구현에서
생성됩니다. Python ≥ 3.9 필요, 유일한 의존성은
[`cryptography`](https://cryptography.io) (PyCA, AES 용)입니다.

원본: [GitHub의 `python/`](https://github.com/algorix-hq/dealcode/tree/main/python)
· [전체 README](https://github.com/algorix-hq/dealcode/blob/main/python/README.md)

## 설치

```sh
pip install dealcode
```


## 최소 예제

```python
from dealcode import Dealcode

codec = Dealcode(key="0a1b...시크릿-매니저에서-가져온-64자-hex")

codec.encode(0)        # '6005d7'   (6자리 hex)
codec.encode(1)        # 'd4e705'   다른 어떤 카운터와도 충돌하지 않음
codec.decode("d4e705") # 1
```

## API 요약

| 항목 | 비고 |
|------|------|
| `Dealcode(key, alphabet="hex", min_length=6, max_length=None, domain="")` | 불변, 스레드 안전; 네임스페이스당 하나 만들어 재사용 |
| `codec.encode(n) -> str` | `[0, codec.capacity)` 밖이면 `RangeError` |
| `codec.decode(code) -> int` | 형식이 잘못된 입력이면 `InvalidCodeError` |
| `CyclingDealcode(key, alphabet, length=6, domain="")` + `cycle_of(n)` | 고정 길이 순환 모드, SPEC §11 — [설정 가이드](../guide/configuration.ko.md#fixed-length-cycling-mode) 참고 |
| 에러 | `ConfigError`, `RangeError`, `InvalidCodeError` — 모두 `DealcodeError`(`ValueError`)의 서브클래스 |

인코딩은 AES-CBC-MAC 10라운드 — 수십 마이크로초, 카운터 값에 대해
O(1)입니다.

## 테스트

```sh
pip install -e ./python pytest    # 또는: export PYTHONPATH=python/src
python -m pytest python/tests
```

NIST 공식 FF1 샘플 벡터, 공유 언어 공통 벡터 전부, 동작/엣지 케이스를
검사합니다.
