# ADR 0004. C++/IOCP Project Baseline

## Status

Accepted

## Decision

이 프로젝트의 주 구현 대상은 C++20과 Windows IOCP 기반 World Server다. Windows-native networking, IO worker infrastructure, server-authoritative gameplay와 명확한 ownership/lifetime 경계를 한 Visual Studio solution에서 구현하고 검증한다.

공식 build와 runtime 검증 환경은 Windows + Visual Studio solution이다. 다른 운영체제는 문서 작성과 구조 검토에 사용할 수 있지만 IOCP와 Winsock runtime behavior의 기준 환경은 아니다.

외부 source tree를 복사해 기반으로 삼지 않는다. 필요한 pattern은 현재 코드, ADR와 공식 Windows/Winsock 문서를 확인한 뒤 이 프로젝트의 ownership과 lifecycle 계약에 맞게 구현한다.

## Consequences

- Thin 2D client는 서버 동작을 표현하고 end-to-end flow를 검증하는 consumer다.
- Production MMORPG 규모, seamless migration, account/persistence와 cloud orchestration은 현재 범위가 아니다.
- 플랫폼 종속 networking 구현은 `PrivateServer.NetworkRuntime` 내부에 캡슐화하고 World와 Client에는 public contract만 노출한다.
