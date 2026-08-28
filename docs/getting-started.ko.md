# 시작하기

모든 구현은 같은 작은 API를 그대로 따릅니다: 코덱을 만들고(키, 알파벳,
최소/최대 길이, 도메인) `encode` / `decode`를 호출합니다. 아래 예제는 각
구현의 README에서 그대로 가져온 것으로 — 키 규칙도, 출력도 모든 언어에서
비트 단위로 동일합니다.


## 퀵스타트

=== "Python"

    ```sh
    pip install dealcode
    ```

    Python ≥ 3.9. 유일한 의존성은 [`cryptography`](https://cryptography.io) (PyCA)입니다.

    ```python
    from dealcode import Dealcode

    codec = Dealcode(key="0a1b...시크릿-매니저에서-가져온-64자-hex")

    codec.encode(0)        # '6005d7'   (6자리 hex)
    codec.encode(1)        # 'd4e705'   다른 어떤 카운터와도 충돌하지 않음
    codec.decode("d4e705") # 1
    ```

=== "TypeScript / JavaScript"

    ```sh
    npm install dealcode
    ```

    Node.js ≥ 18. 런타임 의존성 0(`node:crypto`), ESM + CommonJS 빌드와
    TypeScript 타입 제공.

    ```ts
    import { Dealcode } from "dealcode";

    const codec = new Dealcode({ key: process.env.DEALCODE_KEY! });

    codec.encode(0);        // 예: '6005d7' (6자리 hex; 키에 따라 다름)
    const code = codec.encode(1);    // 다른 어떤 카운터와도 충돌하지 않음
    codec.decode(code);              // 1n  (bigint — 카운터는 2^53을 넘을 수 있음)
    codec.decodeNumber(code);        // 1   (number; MAX_SAFE_INTEGER 초과 시 throw)
    ```

=== "Go"

    ```sh
    go get github.com/algorix-hq/dealcode/go
    ```

    Go ≥ 1.21. 표준 라이브러리만 사용합니다.

    ```go
    import dealcode "github.com/algorix-hq/dealcode/go"

    codec, err := dealcode.New(dealcode.Config{
    	KeyString: "0a1b...시크릿-매니저에서-가져온-64자-hex",
    })
    if err != nil {
    	log.Fatal(err)
    }

    codec.Encode(0)        // "6005d7", nil   (6자리 hex)
    codec.Encode(1)        // "d4e705", nil   다른 어떤 카운터와도 충돌하지 않음
    codec.Decode("d4e705") // 1, nil
    ```

=== "Java"

    ```xml
    <dependency>
      <groupId>io.algorix</groupId>
      <artifactId>dealcode</artifactId>
      <version>1.0.0</version>
    </dependency>
    ```

    Java 17+. 런타임 의존성 0(JCE 내장).

    ```java
    import io.algorix.dealcode.Dealcode;

    Dealcode codec = Dealcode.builder()
            .key("0a1b...시크릿-매니저에서-가져온-64자-hex")
            .build();

    codec.encode(0);        // "6005d7"   (6자리 hex)
    codec.encode(1);        // "d4e705"   다른 어떤 카운터와도 충돌하지 않음
    codec.decode("d4e705"); // 1
    ```

=== "Rust"

    ```sh
    cargo add dealcode
    ```

    Rust ≥ 1.75. 런타임 의존성은 감사된 RustCrypto 크레이트 `aes`,
    `sha2`뿐입니다.

    ```rust
    use dealcode::Dealcode;

    let codec = Dealcode::new("0a1b...시크릿-매니저에서-가져온-64자-hex")?;

    codec.encode(0)?;        // "6005d7"   (6자리 hex)
    codec.encode(1)?;        // "d4e705"   다른 어떤 카운터와도 충돌하지 않음
    codec.decode("d4e705")?; // 1
    ```

=== "C"

    ```sh
    make            # c/ 디렉터리에서 — 정적 라이브러리 libdealcode.a 빌드
    cc -Ic/include myapp.c c/libdealcode.a -lcrypto
    ```

    `unsigned __int128`을 지원하는 C11 컴파일러(GCC/Clang)와 OpenSSL
    libcrypto 1.1+/3.x가 필요합니다.

    ```c
    #include <dealcode.h>

    dealcode_config_t cfg = {0};
    cfg.key_string = "example-key";   /* 문자열 규칙: 항상 SHA-256 파생      */
    cfg.alphabet   = "hex";
    cfg.domain     = "orders";

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

=== "C++"

    ```sh
    cmake -S cpp -B cpp/build && cmake --build cpp/build
    ```

    C 코어를 감싸는 C++17 헤더 온리 래퍼(RAII, 예외, `std::string`). C
    코어와 OpenSSL libcrypto를 링크합니다.

    ```cpp
    #include <dealcode.hpp>

    dealcode::Options opts;
    opts.alphabet = "hex";
    opts.domain = "orders";

    dealcode::Codec codec("example-key", opts);   // 문자열 키 규칙 (파생)

    std::string code = codec.encode(42);          // 예: "59e5f2"
    uint64_t n = codec.decode(code);              // 42
    ```

## 키

키는 AES 원시 바이트(16/24/32바이트는 그대로 AES 키로 사용)도 되고,
**아무 문자열/바이트나** 됩니다 — `openssl rand -hex 32` 출력, 패스프레이즈,
KMS blob. AES 크기가 아닌 재료는 모든 언어에서 동일하게 결정적으로
확장됩니다(`SHA-256("dealcode/v1/kdf" ‖ 재료)`).

```sh
openssl rand -hex 32
```

키는 한 번 만들어 시크릿 매니저에 보관하고, 운영 중인 네임스페이스에서는
절대 바꾸지 마세요 — 매핑은 키(와 다른 모든 옵션)가 고정된 동안만
안정적입니다. 상세와 함정: [설정](guide/configuration.ko.md).

## 모양 고르기

```python
Dealcode(key, "crockford", domain="coupons")       # 사람 친화적, 예: '7Q4WKZ'
Dealcode(key, "dec", min_length=8, domain="orders")  # 숫자만
Dealcode(key, "hex", min_length=16, max_length=16)   # 고정 길이 토큰
```

모든 언어가 같은 네 가지 옵션 — `alphabet`, `min_length`, `max_length`,
`domain` — 을 각 언어의 관용구로 노출합니다(JS는 `minLength`, Java 빌더는
`.minLength(...)` 등). 전체 알파벳 표와 규칙은
[설정](guide/configuration.ko.md)에 있습니다.

## decode가 증명하는 것과 증명하지 않는 것

`decode`는 **형식이 잘못된** 입력(잘못된 길이, 알파벳 밖의 문자, 발급
가능 범위 밖의 값)을 각 언어의 invalid-code 에러로 거절합니다 — DB에 닿기
전에요. 하지만 *형식이 올바른* 코드는 실제로 발급됐는지와 무관하게 항상
어떤 카운터로 복호화됩니다 — 순열의 본질입니다. decode는 존재 증명이
아니라 파싱으로 취급하세요: 카운터를 조회한 뒤에 행동하고, 유효한 코드의
한 글자 오타가 *다른* 유효한 카운터로 풀릴 수 있다는 점도 기억하세요 —
레이트 리밋을 걸고, 사람이 입력하는 플로우라면 존재 확인이나 자체 체크
디지트를 더하세요.

## 다음 단계

- DB에 연결하기: [데이터베이스 연동](guide/database.ko.md)
- 알파벳, 도메인, 길이, 키 규칙: [설정](guide/configuration.ko.md)
- 키가 지키는 것과 지키지 않는 것: [보안 모델](guide/security.ko.md)

코드 공간이 다 차더라도 코드 길이가 영원히 그대로여야 한다면, 설정
가이드의 [고정 길이 순환 모드](guide/configuration.ko.md#fixed-length-cycling-mode)를
보세요.
