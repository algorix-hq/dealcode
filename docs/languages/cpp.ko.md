# C++

[C 코어](c.ko.md)를 감싸는 C++17 래퍼 — 알고리즘은 C에서 한 번 구현되고,
이 래퍼는 RAII, 예외, `std::string` 사용성을 더합니다. 헤더 온리입니다:
`dealcode.hpp`를 include하고 C 코어와 OpenSSL libcrypto를 링크하세요.

원본: [GitHub의 `cpp/`](https://github.com/algorix-hq/dealcode/tree/main/cpp)
· [전체 README](https://github.com/algorix-hq/dealcode/blob/main/cpp/README.md)

## 설치

CMake 서브프로젝트로 소비합니다 — 패키지 레지스트리 단계가 없습니다:

```sh
cmake -S cpp -B cpp/build
cmake --build cpp/build
ctest --test-dir cpp/build
```

다른 CMake 프로젝트에서는 `add_subdirectory(cpp)` 후 `dealcode_cpp`
인터페이스 라이브러리를 링크하면 됩니다(C 코어 `dealcode_core`는
프로젝트가 대신 빌드합니다).

## 최소 예제

```cpp
#include <dealcode.hpp>

dealcode::Options opts;
opts.alphabet = "hex";        // 프리셋 이름 또는 커스텀 알파벳 문자
opts.min_length = 6;
opts.domain = "orders";       // FF1 tweak에 바인딩되는 네임스페이스 라벨

dealcode::Codec codec("example-key", opts);   // 문자열 키 규칙 (파생)

std::string code = codec.encode(42);          // 예: "59e5f2"
uint64_t n = codec.decode(code);              // 42
```

## API 요약

| 항목 | 비고 |
|------|------|
| `dealcode::Codec(key, opts)` | 키는 string, `std::vector<std::uint8_t>`, 또는 `(ptr, len)` |
| `codec.encode(n)` / `codec.decode(code)` | 카운터 ↔ 코드 전단사 |
| `codec.capacity()`, `min_length()`, `max_length()`, `radix()`, `alphabet()` | 인트로스펙션 |
| `dealcode::CycleCodec(key, opts)` | 고정 길이 순환 모드, SPEC §11 — [설정 가이드](../guide/configuration.ko.md#fixed-length-cycling-mode) 참고 |
| 예외 | `dealcode::ConfigError`, `dealcode::RangeError`, `dealcode::InvalidCodeError` — 모두 `dealcode::Error`(`std::runtime_error`) 계열 |

`Codec`은 **move 전용**입니다(키 재료를 포함한 C 핸들을 `std::unique_ptr`로
소유). 공유가 필요하면 `std::shared_ptr<const dealcode::Codec>`으로
감싸세요. 모든 멤버 함수는 `const`이고 동시 호출에 안전합니다.

## 테스트

```sh
ctest --test-dir cpp/build
```

NIST 공식 FF1 샘플, 모든 dealcode v1 벡터 설정, 예외 동작, move 시맨틱,
스테이지 경계를 가로지르는 왕복 스윕을 검사합니다.
