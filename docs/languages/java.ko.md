# Java

[스펙](../spec.ko.md)의 Java 구현. Java 17+ 필요. 런타임 의존성 0 — AES와
SHA-256은 JDK 내장 JCE 프로바이더에서 옵니다.

원본: [GitHub의 `java/`](https://github.com/algorix-hq/dealcode/tree/main/java)
· [전체 README](https://github.com/algorix-hq/dealcode/blob/main/java/README.md)

## 설치

```xml
<dependency>
  <groupId>io.algorix</groupId>
  <artifactId>dealcode</artifactId>
  <version>1.0.0</version>
</dependency>
```

아직 Maven Central에 없습니다 — 그 전까지는 체크아웃에서 빌드해(`java/`
에서 `mvn install`) 로컬 저장소에 넣어 쓰세요.

## 최소 예제

```java
import io.algorix.dealcode.Dealcode;

Dealcode codec = Dealcode.builder()
        .key("0a1b...시크릿-매니저에서-가져온-64자-hex")
        .build();

codec.encode(0);        // "6005d7"   (6자리 hex)
codec.encode(1);        // "d4e705"   다른 어떤 카운터와도 충돌하지 않음
codec.decode("d4e705"); // 1
```

## API 요약

| 항목 | 비고 |
|------|------|
| `Dealcode.builder().key(...).alphabet(...).minLength(...).maxLength(...).domain(...).build()` | 키는 `byte[]` 또는 `String` |
| `codec.encode(long n)` | `[0, codec.maxCounter()]` 밖이면 `CounterRangeException` (포함 최댓값 — 공간이 정확히 2^63일 수 있음) |
| `codec.decode(String code)` | 형식이 잘못된 입력이면 `InvalidCodeException` |
| `CyclingDealcode.builder()...length(...).build()` + `cycleOf(n)` | 고정 길이 순환 모드, SPEC §11 — [설정 가이드](../guide/configuration.ko.md#fixed-length-cycling-mode) 참고 |
| `Alphabets` | 프리셋 알파벳 문자열 상수 |
| 에러 | `ConfigException`, `CounterRangeException`, `InvalidCodeException` — 모두 `DealcodeException`(`IllegalArgumentException`)의 서브클래스 |

`Dealcode` 인스턴스는 불변이며 스레드 안전합니다(AES `Cipher`는
`ThreadLocal`로 스레드별 유지) — 네임스페이스당 하나 만들어 공유하세요.

## 테스트

```sh
cd java && mvn test
```

NIST FF1 벡터, 공유 스펙 벡터(저장소 루트의 `../testvectors/`에서 읽음),
동작 테스트를 실행합니다.
