# dealcode

[English](README.md) | **한국어** · **문서:** <https://algorix-hq.github.io/dealcode/ko/>

카운터에서 **절대 겹치지 않는, 랜덤처럼 보이는 코드**를 만드는 라이브러리 —
잘 섞인 카드 덱에서 딜러가 한 장씩 나눠주는 것과 같습니다. 모든 카드는
정확히 한 번씩 나오고, 순서는 랜덤처럼 보이고, 딜러는 몇 장을 돌렸는지만
기억하면 됩니다.

```
카운터:   0        1        2        3        ...      16,777,216
           │        │        │        │                  │
           ▼        ▼        ▼        ▼                  ▼
코드:    9ebb54   19867f   ae3192   4c2a01   ...       58175f7    ← 6자리가 소진된
                                                                    순간에만 7자리로 성장
```

절대 반복되지 않는 정수(DB 시퀀스, auto-increment id)와 비밀 키를 주면,
다음 성질을 가진 짧은 코드를 돌려줍니다.

- **절대 충돌하지 않음** — 매핑이 키 기반 순열(FF1, NIST SP 800-38G)이라
  유일성이 확률이 아니라 수학으로 보장됩니다. 재시도 루프도, 생일 역설도,
  `UNIQUE` 위반 처리 코드도 필요 없습니다.
- **내부 숫자를 노출하지 않음** — 연속된 입력이 흩어진 예측 불가능한 출력이
  됩니다. 주문량, 발급 속도, "내 앞에 몇 명이 있었는지"가 감춰집니다
  ([독일 전차 문제](https://en.wikipedia.org/wiki/German_tank_problem) 방지).
- **가능한 한 짧게 유지** — 코드는 6자(설정 가능)로 시작해, 현재 길이가
  전부 소진됐을 때만 한 글자씩 늘어납니다.
- **복호화 가능** — 키가 있으면 코드를 카운터로 되돌릴 수 있습니다.
  `orders WHERE id = decode(code)`로 바로 조회하면 되고, 형식이 잘못된
  코드는 DB에 가기 전에 걸러집니다.

같은 키 + 같은 설정이면 모든 언어에서 완전히 동일한 매핑이 나옵니다.
`SPEC.md`가 규범이고, 공유 테스트 벡터가 모든 구현을 비트 단위로 맞춥니다.

## 구현체

| 언어 | 디렉터리 | 설치 | 암호화 의존성 |
|------|----------|------|----------------|
| Python | [`python/`](python/) | `pip install dealcode` | [`cryptography`](https://cryptography.io) (PyCA) |
| TypeScript / JavaScript | [`js/`](js/) | `npm install dealcode` | `node:crypto` (내장) |
| Go | [`go/`](go/) | `go get github.com/algorix-hq/dealcode/go` | 표준 라이브러리 |
| Java | [`java/`](java/) | Maven `io.algorix:dealcode` | JCE (내장) |
| Rust | [`rust/`](rust/) | `cargo add dealcode` | RustCrypto `aes`, `sha2` |
| C | [`c/`](c/) | `make install` / 벤더링 (GCC/Clang: `__int128` 필요) | OpenSSL libcrypto |
| C++ | [`cpp/`](cpp/) | CMake (C 코어 래핑) | OpenSSL libcrypto |

> [PyPI](https://pypi.org/project/dealcode/),
> [npm](https://www.npmjs.com/package/dealcode),
> [crates.io](https://crates.io/crates/dealcode)는 출시됐습니다(v1.0.0).
> Go 모듈은 이 저장소에서 바로 받아집니다. Maven Central은 **출시 준비
> 중**이며, 그때까지는 `java/README.md`의 소스 설치 방법을 사용하세요.

그 외 의존성은 의도적으로 0입니다. FF1과 dealcode 레이어는 각 언어에서
NIST 명세로부터 직접 구현했고, NIST 공식 샘플 벡터와 이 레포의 공유
벡터([`testvectors/`](testvectors/))로 검증합니다.

## 60초 훑어보기 (Python 예시, 모든 언어 동일한 구조)

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
Dealcode(key, "!@#$%^&*")                          # 커스텀 알파벳도 그대로 동작
```

- **`alphabet`** — `dec`, `hex`, `base32`, `crockford`, `base36`, `base58`,
  `base62`, `base64url`, 또는 서로 다른 출력 가능 ASCII 2–94자로 된 커스텀
  문자열. 프리셋에는 합리적인 디코드 정규화가 딸려 있습니다(hex는 대소문자
  무시, Crockford는 `O→0`, `I/L→1`).
- **`domain`** — 네임스페이스. 키 하나로 `"orders"`, `"coupons"`,
  `"invites"`마다 서로 무관한 코드 스트림을 만듭니다.
- **`min_length` / `max_length`** — 시작 길이와 최대 길이. 같게 주면 고정
  길이 코드. 항공권 PNR처럼 *절대* 길어지면 안 되는 고정 길이가 필요하면
  순환 모드(`CyclingDealcode`, SPEC §11)를 쓰세요 — 공간이 소진되면 한
  글자를 늘리는 대신 매 사이클 다른 순열로 같은 공간을 다시 채웁니다.
  사이클을 넘으면 코드가 반복되므로 유일성은 사이클 단위로 관리해야
  합니다.
- **`key`** — AES 원시 키(16/24/32바이트) 또는 **아무 문자열/바이트나**
  (`openssl rand -hex 32` 출력, 패스프레이즈, KMS blob). AES 크기가 아닌
  재료는 모든 언어에서 동일한 방식으로 결정적으로 확장됩니다.

## DB에 연결하기

dealcode는 의도적으로 저장소에 무관합니다. 필요한 것은 반복되지 않는 정수
하나뿐이고, 그건 DB가 이미 제일 잘 만듭니다.

```sql
CREATE SEQUENCE order_code_seq AS bigint MINVALUE 0 START WITH 0;
```

```python
n = db.scalar("SELECT nextval('order_code_seq')")   # 잠금 없음, 갭 허용
code = codec.encode(n)                              # 순수 계산
db.execute("INSERT INTO orders (id, code, ...) VALUES (%s, %s, ...)", (n, code))
```

시퀀스는 롤백이 있어도 같은 번호를 두 번 주지 않고(갭은 어차피 코드가
랜덤처럼 보여서 티가 안 남), FF1은 다른 입력에 다른 출력을 보장합니다.
`code` 컬럼의 `UNIQUE` 인덱스는 메커니즘이 아니라 **경보 장치**입니다.
울렸다면 누군가 운영 중인 네임스페이스의 키/설정을 바꾼 것이니, 재시도하지
말고 조사하세요. MySQL 등은 `AUTO_INCREMENT`/identity 컬럼으로 같은 패턴을
쓰면 됩니다(언어별 README 참고).

한 가지 실무 팁: `decode`는 절대 공백을 잘라내지 않습니다 — 복사·붙여넣기로
끝에 공백/개행이 섞인 코드는 invalid로 거부됩니다. 디코드 전에 사용자
입력을 `strip()`/`trim()` 하세요.

## 언제 쓰고, 언제 쓰지 말아야 하나

주문번호, 쿠폰/초대 코드, 티켓 번호, 상담 PIN, 숏링크처럼 **유일하고, 짧고,
내부 정보를 드러내지 않아야 하는** 값에 쓰세요. 카운터가 이미 있(거나 쉽게
만들 수 있)다면 정확히 이 라이브러리의 용도입니다.

세션 토큰, API 키, 비밀번호 재설정 링크에는 쓰지 **마세요** — 코드 공간이
의도적으로 작아서, *인증*하는 값에는 128비트 이상 랜덤 토큰을 써야 합니다.
중앙 카운터 없이 여러 머신에서 ID가 필요하면 UUIDv7/ULID/Snowflake를,
정렬 가능한 ID가 필요하면 UUIDv7/ULID를, OTP는 HOTP/TOTP를 쓰세요. 전체
논증과 대안 비교표, 운영 가이드는 [docs/philosophy.ko.md](docs/philosophy.ko.md)에
있습니다.

**꼭 기억할 한 가지:** 첫 코드가 나가는 순간 키, 알파벳, 길이, 도메인은
동결입니다. 운영 중인 네임스페이스에서 하나라도 바꾸면 기존 코드와 충돌할
수 있습니다. 새로운 체계가 필요하면 → 새 도메인(또는 새 키 + 새 네임스페이스).

## 동작 원리

`encode(n)`은 카운터 범위로 코드 길이 `d`를 정하고(카운터 `< 16^6` → 6자리
hex, `< 16^7` → 7자리, ...), `n`을 `d`자리 숫자로 쓴 뒤 그 숫자를 FF1로
암호화합니다. FF1은 *같은 자릿수의 다른 숫자*를 출력하는 형식 보존
암호이므로, 같은 길이끼리는 순열이라 충돌 불가, 다른 길이끼리는 길이가
달라서 충돌 불가입니다. `decode`는 역방향으로 돌리고 엄격하게 검증합니다.
상세: [SPEC.md](SPEC.md) · 설계 근거: [docs/design.ko.md](docs/design.ko.md).

## 레포 구성

```
SPEC.md            규범 스펙 (format v1) — 모든 구현은 이 문서에서 나옴
testvectors/       NIST FF1 샘플 + 생성된 언어 공통 벡터. 통과 = 적합성
python/ js/ go/ java/ rust/ c/ cpp/    독립적으로 패키징된 언어별 구현
docs/              철학, 설계 근거 (영/한)
scripts/           테스트 벡터 생성기 (Python 레퍼런스 기준)
```

기여와 신규 언어 포팅을 환영합니다 — 두 벡터 파일을 통과하면 적합한
구현입니다. 스펙 변경은 벡터 재생성과 포맷 버전 상향이 필요합니다.
[CONTRIBUTING.md](CONTRIBUTING.md)를 참고하세요. 보안 취약점 의심 사항은
[SECURITY.md](SECURITY.md)에 따라 비공개로 제보해 주시고,
[행동 강령](CODE_OF_CONDUCT.md)을 지켜 주세요.

## 라이선스

[MIT](LICENSE) © Algorix Corporation
