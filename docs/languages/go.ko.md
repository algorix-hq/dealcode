# Go

[스펙](../spec.ko.md)의 Go 구현. Go ≥ 1.21 필요. 표준 라이브러리만
사용합니다 — AES와 SHA-256은 `crypto/aes`, `crypto/sha256`에서 옵니다.

원본: [GitHub의 `go/`](https://github.com/algorix-hq/dealcode/tree/main/go)
· [전체 README](https://github.com/algorix-hq/dealcode/blob/main/go/README.md)

## 설치

```sh
go get github.com/algorix-hq/dealcode/go
```

레지스트리 없이 GitHub에서 바로 동작합니다. 릴리스는 모노레포 관례대로
`go/` 모듈 프리픽스로 태깅됩니다: `go/v1.x.y`.

## 최소 예제

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

## API 요약

| 항목 | 비고 |
|------|------|
| `dealcode.New(dealcode.Config{...}) (*Codec, error)` | `Key []byte` 또는 `KeyString string`(정확히 하나), `Alphabet`, `MinLength`, `MaxLength`, `Domain`; 제로 값은 스펙 기본값 선택 |
| `codec.Encode(n int64) (string, error)` | `[0, codec.Capacity())` 밖이면 `ErrRange`를 감싼 에러 |
| `codec.Decode(code string) (int64, error)` | 형식이 잘못된 입력이면 `ErrInvalidCode`를 감싼 에러 |
| 에러 | 센티널 값 `ErrConfig`, `ErrRange`, `ErrInvalidCode` — `errors.Is`로 분류 |

`Codec`은 불변이고 잠금 없이 여러 고루틴에서 동시에 사용해도 안전합니다.

## 테스트

```sh
cd go && go vet ./... && go test -race ./...
```

NIST 공식 FF1 샘플 벡터, 공유 언어 공통 벡터 전부, 동작 케이스, 레이스
디텍터 아래의 동시 사용을 검사합니다.
