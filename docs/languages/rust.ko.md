# Rust

[스펙](../spec.ko.md)의 Rust 구현. Rust ≥ 1.75 필요. 런타임 의존성은
감사된 [RustCrypto](https://github.com/RustCrypto) 크레이트
[`aes`](https://crates.io/crates/aes)와
[`sha2`](https://crates.io/crates/sha2)뿐입니다.

원본: [GitHub의 `rust/`](https://github.com/algorix-hq/dealcode/tree/main/rust)
· [전체 README](https://github.com/algorix-hq/dealcode/blob/main/rust/README.md)

## 설치

```sh
cargo add dealcode            # crates.io 배포 이후
```

아직 crates.io에 없습니다 — 그 전까지는 git 의존성을 쓰세요:

```toml
[dependencies]
dealcode = { git = "https://github.com/algorix-hq/dealcode" }
```

## 최소 예제

```rust
use dealcode::Dealcode;

let codec = Dealcode::new("0a1b...시크릿-매니저에서-가져온-64자-hex")?;

codec.encode(0)?;        // "6005d7"   (6자리 hex)
codec.encode(1)?;        // "d4e705"   다른 어떤 카운터와도 충돌하지 않음
codec.decode("d4e705")?; // 1
```

## API 요약

| 항목 | 비고 |
|------|------|
| `Dealcode::new(key)` | 기본 설정; 키는 `impl Into<Key>`: `&str`, `String`, `&[u8]`, `Vec<u8>`, `[u8; N]` |
| `Dealcode::builder(key).alphabet(..).min_length(..).max_length(..).domain(..).build()` | 잘못된 설정 → `Err(Error::Config)` |
| `codec.encode(n: u64) -> Result<String>` | `[0, codec.capacity())` 밖이면 `Err(Error::Range)` |
| `codec.decode(code) -> Result<u64>` | 형식이 잘못된 입력이면 `Err(Error::InvalidCode)` |
| 에러 | `dealcode::Error`(`Config` / `Range` / `InvalidCode`), `std::error::Error` 구현 |

`Dealcode` 인스턴스는 불변이고 `Send + Sync`입니다 — `Arc`나 `static`으로
공유하세요. AES 라운드 키와 길이별 FF1 파라미터는 빌드 시점에 미리
계산됩니다.

## 테스트

```sh
cd rust && cargo test && cargo clippy --all-targets -- -D warnings
```

NIST 공식 FF1 샘플 벡터, 공유 언어 공통 벡터 전부, 동작 케이스, doctest를
검사합니다.
