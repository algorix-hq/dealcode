# C

[스펙](../spec.ko.md)의 C11 구현 — [C++ 래퍼](cpp.ko.md)의 기반 코어이기도
합니다. GCC 또는 Clang(`unsigned __int128` 사용)과 OpenSSL libcrypto
1.1+/3.x가 필요하고, 사용하는 쪽은 `-lcrypto`로 링크해야 합니다.

원본: [GitHub의 `c/`](https://github.com/algorix-hq/dealcode/tree/main/c)
· [전체 README](https://github.com/algorix-hq/dealcode/blob/main/c/README.md)

## 설치

의도적으로 벤더링 / 정적 라이브러리 방식입니다 — 패키지 레지스트리 단계가
없습니다:

```sh
make            # c/ 디렉터리에서 — 정적 라이브러리 libdealcode.a 빌드
make test       # 테스트 벡터 생성 후 테스트 스위트 실행

cc -Ic/include myapp.c c/libdealcode.a -lcrypto
```

## 최소 예제

```c
#include <dealcode.h>

dealcode_config_t cfg = {0};
cfg.key_string = "example-key";   /* 문자열 규칙: 항상 SHA-256 파생        */
cfg.alphabet   = "hex";           /* 프리셋 이름 또는 커스텀 알파벳 문자    */
cfg.domain     = "orders";        /* 네임스페이스 라벨, tweak에 바인딩      */

dealcode_t *dc = NULL;
dealcode_err_t err = dealcode_new(&cfg, &dc);
if (err != DEALCODE_OK) {
    fprintf(stderr, "dealcode: %s\n", dealcode_strerror(err));
    return 1;
}

char code[DEALCODE_MAX_CODE_SIZE];
dealcode_encode(dc, 42, code, sizeof code);   /* -> 예: "59e5f2" */

uint64_t n;
dealcode_decode(dc, code, &n);                /* -> 42 */

dealcode_free(dc);
```

## API 요약

| 항목 | 비고 |
|------|------|
| `dealcode_new` / `dealcode_free` | 코덱 수명주기 — 불투명 핸들, 전역 상태 없음 |
| `dealcode_encode` / `dealcode_decode` | 카운터 ↔ 코드 전단사 |
| `dealcode_capacity`, `dealcode_min_length`, `dealcode_max_length`, `dealcode_radix`, `dealcode_alphabet` | 인트로스펙션 |
| `dealcode_strerror` | 사람이 읽을 수 있는 에러 설명 |
| 에러 | 명시적 `dealcode_err_t` 반환 코드; 실패 시 출력 파라미터에 아무것도 쓰지 않음(`dealcode_new`의 `*out = NULL` 제외) |

전체 문서화된 API는 `include/dealcode.h`에 있습니다. `dealcode_t`는 생성
후 불변이며 스레드 간 자유롭게 공유할 수 있습니다 — encode/decode는 호출마다
새 OpenSSL 사이퍼 컨텍스트를 할당합니다.

## 테스트

```sh
cd c && make test
```

NIST 공식 FF1 샘플 벡터 9개, 모든 dealcode v1 테스트 벡터 설정, 에러 동작,
스테이지 경계를 가로지르는 대규모 왕복 스윕 — `radix^max_length`가 정확히
`2^128`인 설정 포함 — 을 검사합니다.
