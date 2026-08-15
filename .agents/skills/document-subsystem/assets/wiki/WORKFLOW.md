# Wiki Authoring Workflow

> Document status: Draft
> Baseline: Not established
> Last reviewed: Not reviewed

## 목적

현재 코드와 검증 근거를 바탕으로 독자가 바로 읽을 수 있는 subsystem 기술 문서를 작성한다. Wiki는 작업 기록이나 이슈 상태가 아니라 현재 책임, interface, ownership, lifetime와 runtime behavior를 설명한다.

## 작성 단위

한 문서는 하나의 독자 질문에 답한다.

- public/application boundary
- ownership/lifetime design
- 정상 runtime scenario
- failure/pressure/shutdown scenario
- code/test source map

서로 다른 질문은 별도 문서로 분리한다.

## 근거 우선순위

```text
domain context / ADR / code / tests / private evidence
                         |
                         v
                    wiki 문서
```

- 코드와 project file은 현재 구조의 최종 근거다.
- Test는 behavioral contract를 설명한다.
- Smoke와 benchmark artifact는 작성자가 동작을 교차 확인하는 비공개 근거다.
- 테스트 파일의 존재만으로 현재 baseline의 실행 성공을 주장하지 않는다.

## 공개 whitelist

- 각 디렉터리의 `.wiki-documents`에 등록된 Markdown만 공개 대상으로 취급한다.
- 등록 문서는 `wiki/` 안의 unregistered Markdown을 링크하지 않는다.
- 등록 문서는 내부 측정값, benchmark/test 통과 수, load 수치, run ID, 환경 정보, verdict field와 평가 rubric을 포함하지 않는다.
- Raw evidence, benchmark report와 내부 평가 문서는 whitelist 밖에 둔다.
- 코드와 테스트 링크는 contract를 추적하는 용도로만 사용하고 실행 성공이나 수치 결과를 대신 주장하지 않는다.
- 공개 직전에는 validator의 `--all-public` 모드로 모든 manifest 합집합을 한 번에 검사한다.

## 공개 문체

- 독자의 질문과 핵심 답을 먼저 제시한다.
- 책임, 의존 방향, interface, invariant, ownership과 lifetime을 파일 목록보다 먼저 설명한다.
- 중요한 trade-off는 설계 이유로 설명한다.
- 현재 제약은 독자에게 영향을 주는 내용만 `지원 범위와 제약`으로 기록한다.
- tracker 상태, 내부 drift, cleanup 목록, 작성 과정과 포트폴리오 코칭은 Wiki에 넣지 않는다.
- 내부 수치나 평가 기준이 없어도 책임, contract와 failure 의미를 이해할 수 있게 작성한다.

## 다이어그램

다이어그램은 독자 질문을 더 분명하게 만들 때만 추가한다.

- Wiki 문서 하나당 editable source 하나를 기본으로 한다.
- 같은 질문의 build-time/runtime 관점처럼 서로 보완하는 그림은 Frame으로 나눈다.
- Excalidraw 원본은 `diagrams/<slug>.excalidraw`, 공개본은 `diagrams/<slug>.svg`를 사용한다.
- 화살표의 의미와 방향을 명시하고 실제 dependency, ownership와 lifetime에 맞게 검토한다.
- 설명용 책임 묶음을 실제 library, process 또는 runtime component처럼 표현하지 않는다.

## 문서 상태

- `Draft`: 기술 주장, 근거, 문체 또는 요청된 시각 자료에 publication blocker가 남아 있다.
- `Reviewed`: 현재 기준 버전에서 기술 검토와 공개 문체 검사를 통과했다.

`Baseline`과 `Last reviewed`를 함께 기록해 문서가 설명하는 코드 시점을 명확히 한다.
