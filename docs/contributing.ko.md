---
title: 기여하기
---

!!! info "원문 안내"

    아래 내용은 저장소 루트의
    [`CONTRIBUTING.md`](https://github.com/algorix-hq/dealcode/blob/main/CONTRIBUTING.md)를
    그대로 렌더링한 것입니다(영어). 이어지는 "적합성, 실제로는" 절은 이
    사이트에만 있는 보충 설명입니다.

--8<-- "CONTRIBUTING.md"

---

## 적합성, 실제로는

"이 구현은 올바르다"의 기준은 의도적으로 기계적입니다:
[`testvectors/`](https://github.com/algorix-hq/dealcode/tree/main/testvectors)의
파일들을 통과하면 됩니다 (`v1c.json`은 고정 길이 순환 모드용으로, 이 모드를
제공하는 구현에 필수입니다 — 여기 7개 구현 전부 해당).

**`ff1_nist.json`**은 dealcode 자체 레이어와 무관하게 FF1 코어를 NIST 공식
샘플 벡터 9개에 고정합니다. FF1이 맞으면 통과하고, 통과하면 서로 다른 7개
FF1 구현이 같은 순열을 계산한다는 것이 증명됩니다. (NIST 간행물은 미국
퍼블릭 도메인입니다.)

**`v1.json`**은 코덱 전체를 검사합니다: 모든 프리셋 알파벳, 스테이지
경계(코드가 한 글자 자라는 정확한 카운터), 두 가지 키 파생 경로(AES 크기
바이트는 직접 사용 vs 나머지는 파생), 디코드 정규화(`hex` 대소문자,
Crockford `O→0`, `I/L→1`), 도메인, 그리고 반드시 **거절되어야 하는**
코드들 — 잘못된 길이, 잘못된 문자셋, 스테이지 밖, 카운터 공간 밖.
[`scripts/generate_test_vectors.py`](https://github.com/algorix-hq/dealcode/blob/main/scripts/generate_test_vectors.py)가
Python 레퍼런스 구현으로부터 생성하며 카운터는 2^53을 넘기 때문에 JSON
문자열로 인코딩됩니다.

모든 언어의 테스트 스위트가 두 파일을 소비하므로 포팅은 스위트가 초록이
되는 순간 적합합니다 — 판단이 개입할 여지가 없습니다. v1 벡터가 영원히
동결되는 이유이기도 합니다: 이 벡터가 곧 구현들 사이의, 그리고 여러분과
이미 발급된 모든 코드 사이의 호환성 계약*입니다*.
