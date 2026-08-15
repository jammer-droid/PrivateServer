# Private Server

Windows IOCP 기반 MMO-lite 서버 아키텍처를 구현하고 검증하는 C++20 프로젝트다. 서버 권위 gameplay, 명확한 session/actor/World ownership, AOI replication, data-driven Host 설정과 재현 가능한 runtime 검증을 주요 목표로 한다.

## 주요 구성

- `PrivateServer.NetworkRuntime`: IOCP 기반 public networking runtime DLL
- `PrivateServer.WorldServer`: authoritative World simulation과 gameplay static library
- `PrivateServer.WorldServer.Host`: Channel 하나와 독립 World instance 하나를 소유하는 process Host
- `PrivateServer.GameClient`: Player와 read-only Observer mode로 서버 동작을 시각화하고 검증하는 Godot C# client
- `PrivateServer.NetworkRuntime.Benchmark`: lifecycle, 부하와 성능 artifact를 생성하는 controller

World Host process 하나는 Channel 하나를 소유한다. 로컬 환경에서는 서로 다른 endpoint의 Host를 여러 개 실행하고, Game Client에서 채널과 일회성 닉네임을 선택해 접속할 수 있다.

Observer mode는 Player ID, controlled entity와 input authority 없이 Channel-wide `WorldOverview`와 round 결과를 표현한다. Benchmark fleet의 `-LaunchObservers`는 두 Channel을 Godot 창으로 함께 확인하는 촬영·시각 검증 entrypoint이며, 추가 session과 rendering 부하가 생기므로 canonical 성능 비교에는 사용하지 않는다.

## 시작하기

필요한 개발환경, Debug/Release 빌드, 단일·다중 Host와 Game Client 실행, benchmark 사용법은 [tools 사용 가이드](tools/README.md)를 따른다.

## 저장소 구조

| 경로 | 역할 |
| --- | --- |
| `src/` | NetworkRuntime, WorldServer, Host, benchmark와 Game Client 구현 |
| `config/` | World Host, fleet와 benchmark 설정 |
| `tools/` | 빌드, 로컬 실행과 benchmark wrapper |
| `docs/` | ADR와 반복 적용되는 구현 convention |
| `wiki/` | 공개 subsystem 설명과 source navigation |
| `artifacts/` | 실행·benchmark 과정에서 생성되는 Git 제외 결과 |

## 문서 진입점

- [Wiki](wiki/README.md): 전체 구조와 subsystem별 주요 source navigation
- [프로젝트 source map](wiki/project-source-map.md): 구현, 테스트, 설정과 도구 위치
- [ADR](docs/adr/): 주요 기술 결정
- [구현 convention](docs/design/conventions/): C++, DLL, error, IO hot path와 Runtime/World 경계 규칙

## 지원 범위

현재 범위는 Windows-native IOCP, 단일 process당 하나의 Channel, 로컬 다중 Host와 thin 2D client다. Production MMORPG 규모, seamless migration, account/persistence와 cloud orchestration은 포함하지 않는다.

## Third-party software and assets

- Box2D: [`src/vendor/box2d/LICENSE`](src/vendor/box2d/LICENSE)
- spdlog와 bundled fmt: [`src/vendor/spdlog/LICENSE`](src/vendor/spdlog/LICENSE)
- Game Client fonts, audio와 visual assets: [`src/PrivateServer.GameClient/Assets/ATTRIBUTION.md`](src/PrivateServer.GameClient/Assets/ATTRIBUTION.md)
