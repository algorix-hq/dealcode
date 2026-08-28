# dealcode

**카운터에서 절대 겹치지 않는, 랜덤처럼 보이는 코드를** — 잘 섞인 카드
덱에서 딜러가 한 장씩 나눠주는 것과 같습니다. 모든 카드는 정확히 한 번씩
나오고, 순서는 랜덤처럼 보이고, 딜러는 몇 장을 돌렸는지만 기억하면 됩니다.

```
카운터:   0        1        2        3        ...      16,777,216
           │        │        │        │                  │
           ▼        ▼        ▼        ▼                  ▼
코드:    9ebb54   19867f   ae3192   4c2a01   ...       58175f7    ← 6자리가 소진된
                                                                    순간에만 7자리로 성장
```

절대 반복되지 않는 정수(DB 시퀀스, auto-increment id)와 비밀 키를 주면,
다음 네 가지 성질을 가진 짧은 코드를 돌려줍니다.

<div class="grid cards" markdown>

- :material-cards-playing-outline: **절대 충돌하지 않음**

    ---

    매핑이 키 기반 순열(FF1, NIST SP 800-38G)이라 유일성이 확률이 아니라
    수학으로 보장됩니다. 재시도 루프도, 생일 역설도, `UNIQUE` 위반 처리
    코드도 필요 없습니다.

- :material-eye-off-outline: **내부 숫자를 노출하지 않음**

    ---

    연속된 입력이 흩어진 예측 불가능한 출력이 됩니다. 주문량, 발급 속도,
    "내 앞에 몇 명이 있었는지"가 감춰집니다
    ([독일 전차 문제](https://en.wikipedia.org/wiki/German_tank_problem) 방지).

- :material-arrow-collapse-horizontal: **가능한 한 짧게 유지**

    ---

    코드는 6자(설정 가능)로 시작해, 현재 길이가 전부 소진됐을 때만 한
    글자씩 늘어납니다.

- :material-swap-horizontal: **복호화 가능**

    ---

    키가 있으면 코드를 카운터로 되돌릴 수 있습니다.
    `orders WHERE id = decode(code)`로 바로 조회하면 되고, 형식이 잘못된
    코드는 DB에 가기 전에 걸러집니다.

</div>

같은 키 + 같은 설정이면 모든 언어에서 완전히 동일한 매핑이 나옵니다.
[스펙](spec.ko.md)이 규범이고, 공유 테스트 벡터가 7개 구현을 비트 단위로
맞춥니다.

## 60초 훑어보기

Python 예시입니다. [모든 언어가 같은 구조](getting-started.ko.md)입니다.

```python
from dealcode import Dealcode

codec = Dealcode(key="운영에서는 `openssl rand -hex 32` 값을 쓰세요")

codec.encode(0)          # '9ebb54'
codec.encode(1)          # '19867f'
codec.decode("19867f")   # 1
```

제품에 맞는 모양을 고르면 됩니다:

```python
Dealcode(key, "crockford", domain="coupons")       # 예: 'ZV6NQ0' — 사람 친화적, 혼동 문자 자동 처리
Dealcode(key, "dec",       domain="orders")        # 예: '839207' — 숫자만
Dealcode(key, "base62",    min_length=8)           # 예: 'tHx93bQk'
Dealcode(key, "hex", min_length=16, max_length=16) # 고정 길이 토큰
CyclingDealcode(key, "crockford", length=6)        # 영원히 고정 길이 — 사이클마다 공간 재사용 (가이드 참고)
Dealcode(key, "!@#$%^&*")                          # 커스텀 알파벳도 그대로 동작
```

## 7개 구현, 하나의 매핑

| 언어 | 디렉터리 | 설치 | 암호화 의존성 |
|------|----------|------|----------------|
| [Python](languages/python.ko.md) | [`python/`](https://github.com/algorix-hq/dealcode/tree/main/python) | `pip install dealcode` | [`cryptography`](https://cryptography.io) (PyCA) |
| [TypeScript / JavaScript](languages/js.ko.md) | [`js/`](https://github.com/algorix-hq/dealcode/tree/main/js) | `npm install dealcode` | `node:crypto` (내장) |
| [Go](languages/go.ko.md) | [`go/`](https://github.com/algorix-hq/dealcode/tree/main/go) | `go get github.com/algorix-hq/dealcode/go` | 표준 라이브러리 |
| [Java](languages/java.ko.md) | [`java/`](https://github.com/algorix-hq/dealcode/tree/main/java) | Maven `io.algorix:dealcode` | JCE (내장) |
| [Rust](languages/rust.ko.md) | [`rust/`](https://github.com/algorix-hq/dealcode/tree/main/rust) | `cargo add dealcode` | RustCrypto `aes`, `sha2` |
| [C](languages/c.ko.md) | [`c/`](https://github.com/algorix-hq/dealcode/tree/main/c) | 벤더링 / 정적 라이브러리 | OpenSSL libcrypto |
| [C++](languages/cpp.ko.md) | [`cpp/`](https://github.com/algorix-hq/dealcode/tree/main/cpp) | C 코어 래핑 | OpenSSL libcrypto |

!!! note "레지스트리 배포 상태"

    패키지는 아직 PyPI, npm, crates.io, Maven Central에 **배포되지
    않았습니다** — 위 설치 명령은 릴리스가 나간 뒤의 명령입니다. 지금은
    저장소에서 직접 설치하세요. `go get`은 이미 GitHub에서 바로 동작하고,
    나머지 언어는 체크아웃에서 설치합니다(각
    [언어 페이지](languages/python.ko.md) 참고). C와 C++는 원래부터
    벤더링 방식입니다.

그 외 의존성은 의도적으로 0입니다. FF1과 dealcode 레이어는 각 언어에서
NIST 명세로부터 직접 구현했고, NIST 공식 샘플 벡터와 이 레포의 공유
벡터([`testvectors/`](https://github.com/algorix-hq/dealcode/tree/main/testvectors))로
검증합니다.

## 동작 원리

`encode(n)`은 카운터 범위로 코드 길이 `d`를 정하고(카운터 `< 16^6` → 6자리
hex, `< 16^7` → 7자리, ...), `n`을 `d`자리 숫자로 쓴 뒤 그 숫자를 FF1로
암호화합니다. FF1은 *같은 자릿수의 다른 숫자*를 출력하는 형식 보존
암호이므로, 같은 길이끼리는 순열이라 충돌 불가, 다른 길이끼리는 길이가
달라서 충돌 불가입니다. `decode`는 역방향으로 돌리고 엄격하게 검증합니다.

상세: [스펙](spec.ko.md) · 설계 근거: [설계 결정 기록](design.ko.md) ·
문제 정의: [dealcode가 존재하는 이유](philosophy.ko.md).

## 언제 쓰고, 언제 쓰지 말아야 하나

주문번호, 쿠폰/초대 코드, 티켓 번호, 상담 PIN, 숏링크처럼 **유일하고,
짧고, 내부 정보를 드러내지 않아야 하는** 값에 쓰세요. 카운터가 이미
있(거나 쉽게 만들 수 있)다면 정확히 이 라이브러리의 용도입니다.

세션 토큰, API 키, 비밀번호 재설정 링크에는 쓰지 **마세요** — 코드 공간이
의도적으로 작아서, *인증*하는 값에는 128비트 이상 랜덤 토큰을 써야 합니다.
전체 논증과 대안 비교표는 [dealcode가 존재하는
이유](philosophy.ko.md)에, 위협 모델은 [보안 모델](guide/security.ko.md)에
있습니다.

!!! danger "꼭 기억할 한 가지"

    첫 코드가 나가는 순간 키, 알파벳, 길이, 도메인은 **동결**입니다. 운영
    중인 네임스페이스에서 하나라도 바꾸면 기존 코드와 충돌할 수 있습니다.
    새로운 체계가 필요하면 → 새 도메인(또는 새 키 + 새 네임스페이스).

## 라이선스

[MIT](https://github.com/algorix-hq/dealcode/blob/main/LICENSE) © Algorix
Corporation.
