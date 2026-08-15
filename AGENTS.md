# Private Server Agent Guide

## 목표

`PrivateServer`는 Windows-native C++/IOCP networking, server-authoritative gameplay와 명확한 ownership/lifetime 경계를 구현하고 검증하는 MMO-lite 서버 프로젝트다.

작업 결과는 다음 방향을 유지해야 한다.

- 서버 권위 gameplay와 data-driven 확장성
- Windows-native IOCP 네트워킹과 명확한 메모리·수명 소유권
- Channel, AOI, actor ownership 같은 실제 game-server 경계
- lifecycle, 부하, failure와 성능을 재현할 수 있는 검증 경로

Thin 2D client는 서버 동작을 보여주는 표현 계층이다. Production MMORPG 규모, seamless MMO와 cloud-first 운영은 현재 범위가 아니다.

## 완료 기준

작업을 마치기 전에 다음이 참이어야 한다.

- 요청한 결과가 실제 코드나 문서에 반영됐다.
- 변경은 요청 범위 안에서 가장 작은 충분한 형태다.
- 관련 build, test, smoke 또는 문서 검사를 수행했거나 수행하지 못한 이유와 검증 방법을 밝혔다.
- 새로운 용어, ownership, lifecycle 또는 public contract가 기존 문서와 충돌하지 않는다.
- 남은 blocker나 중요한 가정이 있다면 명시했다.

## 컨텍스트 라우팅

비사소한 작업에서는 필요한 자료만 다음 순서로 확인한다.

1. `CONTEXT.md`: 프로젝트 고유 용어와 범위
2. `docs/adr/`: 주요 기술 결정
3. `docs/design/conventions/`: 반복 적용되는 구현 계약
4. `wiki/README.md`와 `wiki/project-source-map.md`: subsystem 설명과 source navigation
5. 관련 코드, 프로젝트 파일과 테스트

이미 확정된 답을 찾을 수 있는 로컬 자료를 먼저 확인하고, 결과를 바꿀 수 있는 정보가 없을 때만 질문한다.

## 작업 권한

- 설명, 검토, 진단, 계획 요청은 관련 자료를 읽고 결과를 보고한다. 변경 요청이 없으면 파일이나 외부 시스템을 수정하지 않는다.
- 구현, 수정, 작성 요청은 범위 안의 로컬 변경과 비파괴 검증까지 진행한다.
- 외부 write, 파괴적 작업, 비용 발생 또는 실질적인 범위 확대는 명시적 확인을 받는다.
- 여러 해석이 결과를 실질적으로 바꿀 때는 가정과 trade-off를 먼저 밝힌다. 그렇지 않으면 합리적인 가정으로 진행한다.

## 구현 규칙

- 가장 단순한 충분한 해법을 선택하고 사용자 작업과 무관한 코드를 정리하지 않는다.
- Core C++/IOCP networking, protocol, memory lifetime과 performance evidence를 구현 편의로 생략하지 않는다.
- 확장성이 작업의 핵심이면 gameplay/content 정의를 hard-code하지 않고 data-driven 경계를 우선한다.
- 외부 학습 저장소에서 source tree를 복사하지 않는다. 필요한 pattern은 현재 설계와 공식 Windows/Winsock 문서를 기준으로 다시 구현한다.
- C++ 코드에서는 `auto`를 사용하지 않고 변수와 반환값의 구체적인 타입을 명시한다.

### Learning Gate

Packet framing, session lifecycle, actor/runtime ownership, World simulation, AOI, persistence consistency, drain/shutdown과 C++/IOCP networking처럼 학습 비중이 큰 구현은 다음 순서를 적용한다.

- 핵심 개념과 ownership을 먼저 확인한다.
- 한 번에 하나의 reviewable slice만 구현하고 검증한다.
- 각 slice 뒤에는 사용자의 확인을 기다린다.

Scaffolding, build wiring, 단순 DTO/config plumbing과 반복적인 문서·CRUD 작업은 빠른 경로로 처리할 수 있다.

### Visual Studio C++

- 새 C++ 파일이나 project entry가 필요한 slice는 먼저 논리적 파일 트리를 제안하고 사용자가 Visual Studio 구성을 준비할 때까지 기다린다.
- `.vcxproj.filters`는 사용자가 명시적으로 요청하지 않는 한 수정하지 않는다.
- 사용자가 C++ 파일과 project/filter 설정을 직접 맡긴 경우에도 새 물리 하위 디렉터리는 만들지 않는다. 기존 물리 파일 배치를 따르고 계층 구분은 논리 필터로 표현한다.
- 기본 검증 환경은 Windows + Visual Studio solution이다. 사용자가 별도로 요청하지 않으면 정확한 build target/command를 안내하고 사용자가 실행한다.
- Runtime 내부 구현 검증은 `PrivateServer.NetworkRuntime.Internal.lib`와 InternalTests seam을 사용한다. 테스트 편의를 위해 internal type을 DLL public API로 export하지 않는다.
- Public consumer 검증은 public NetworkRuntime header와 DLL/import library 경계를 유지한다.

## 문서 책임

- `CONTEXT.md`: 안정된 프로젝트 고유 용어와 bounded-context 경계
- `docs/adr/`: 주요 기술 결정과 근거
- `docs/design/conventions/`: 반복 적용되는 구현 규칙
- `wiki/`: 공개 subsystem 설명, runtime scenario와 source navigation
- `.agents/skills/document-subsystem/`: 선택한 subsystem Wiki를 현재 코드와 테스트에 맞춰 갱신하는 명시적 진입점

`CONTEXT.md`에 구현 단계나 임시 계획을 넣지 않는다. 안정된 설계가 바뀌면 관련 ADR, convention과 Wiki 설명에 미친 영향을 함께 확인한다.

## 커뮤니케이션

- 사용자의 최신 언어로 답한다.
- 결론을 먼저 말하고 필요한 근거, 중요한 caveat와 다음 행동을 보존한다.
- 설명은 `문제 -> 메커니즘 -> 증거` 흐름을 선호한다.
- 도구를 사용하는 긴 작업은 시작 전 목적을 알리고 판단이 바뀌는 주요 단계에서만 짧게 업데이트한다.
