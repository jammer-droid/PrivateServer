# {{PROJECT_NAME}} Wiki

> Document status: Draft
> Baseline: Not established
> Last reviewed: Not reviewed

이 디렉터리는 현재 코드와 테스트를 바탕으로 작성한 공개 준비형 subsystem 기술 문서를 관리한다.

## Source of truth

```text
domain context / decisions / code / tests
                         |
                         v
                  wiki 기술 문서
```

- 코드와 테스트가 실제 동작의 최종 근거다.
- 결정 문서는 선택의 이유와 이력을 소유한다.
- wiki는 기준 버전에서 독자가 이해할 현재 설명을 소유한다.
- `.wiki-documents`에 등록된 Markdown만 공개 대상으로 취급한다.
- 등록 문서는 내부 측정값, benchmark/test 수치와 평가 기준을 포함하지 않는다.

## 문서 metadata

```text
Document status: Draft | Reviewed
Baseline: commit | tag | Not established
Last reviewed: YYYY-MM-DD | Not reviewed
```

Wiki metadata는 문서 완성도와 설명 기준만 나타낸다.

## 시작점

1. 이 페이지와 등록된 subsystem 문서에서 현재 책임과 실행 흐름을 확인한다.
2. `.wiki-documents`에 등록되지 않은 Markdown은 공개 페이지에서 링크하지 않는다.
3. 구현되지 않은 내용을 현재 기능처럼 작성하지 않는다.
4. 문서에는 독자에게 필요한 현재 범위와 제약만 기록한다.
5. Raw evidence와 내부 평가 자료는 whitelist 밖에 유지한다.

## Subsystem

- [{{SUBSYSTEM_TITLE}}]({{SUBSYSTEM_SLUG}}/README.md)
