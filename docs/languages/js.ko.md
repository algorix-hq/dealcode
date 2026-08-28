# TypeScript / JavaScript

[스펙](../spec.ko.md)의 TypeScript 구현. Node.js ≥ 18 필요. 런타임 의존성
0 — AES와 SHA-256은 `node:crypto`에서 옵니다. ESM + CommonJS 빌드와 전체
TypeScript 타입을 제공합니다.

원본: [GitHub의 `js/`](https://github.com/algorix-hq/dealcode/tree/main/js)
· [전체 README](https://github.com/algorix-hq/dealcode/blob/main/js/README.md)

## 설치

```sh
npm install dealcode
```


## 최소 예제

```ts
import { Dealcode } from "dealcode";

const codec = new Dealcode({ key: process.env.DEALCODE_KEY! });

codec.encode(0);        // 예: '6005d7' (6자리 hex; 키에 따라 다름)
const code = codec.encode(1);    // 다른 어떤 카운터와도 충돌하지 않음
codec.decode(code);              // 1n  (bigint — 카운터는 2^53을 넘을 수 있음)
codec.decodeNumber(code);        // 1   (number; MAX_SAFE_INTEGER 초과 시 throw)
```

## API 요약

| 항목 | 비고 |
|------|------|
| `new Dealcode({ key, alphabet, minLength, maxLength, domain })` | 불변(frozen), 동시 사용 안전 |
| `codec.encode(n)` | `number` 또는 `bigint`; 범위 밖이면 `CounterRangeError` |
| `codec.decode(code) -> bigint` / `codec.decodeNumber(code) -> number` | 형식이 잘못된 입력이면 `InvalidCodeError` |
| 읽기 전용 프로퍼티 | `alphabet`, `radix`, `minLength`, `maxLength`, `domain`, `capacity` (bigint) |
| `new CyclingDealcode({ key, alphabet, length, domain })` + `cycleOf(n)` | 고정 길이 순환 모드, SPEC §11 — [설정 가이드](../guide/configuration.ko.md#fixed-length-cycling-mode) 참고 |
| export | 프리셋 알파벳 문자열 `ALPHABETS`; 에러는 `DealcodeError` 확장(`ConfigError`, `CounterRangeError`, `InvalidCodeError`) |

## 테스트

```sh
cd js && npm install && npm run build && npm test
```

NIST 공식 FF1 샘플 벡터, 공유 언어 공통 벡터 전부, 컴파일 결과물에 대한
동작/엣지 케이스를 검사합니다.
