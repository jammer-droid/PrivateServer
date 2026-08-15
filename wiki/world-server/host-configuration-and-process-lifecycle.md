# World Host 설정과 Process Lifecycle

> Document status: Reviewed
> Baseline: c0bd3a8e5f1861c6dc1321381b6c58ca7a374030
> Last reviewed: 2026-08-16

## 핵심 답

`PrivateServer.WorldServer.Host`는 Channel 하나의 configuration과 process lifetime을 소유하는 composition root다. Host는 JSON config를 strict schema로 읽고 검증한 뒤 NetworkRuntime, World simulation, replication과 gameplay config로 분해한다. 그 다음 logging과 stop control을 먼저 준비하고 Runtime과 World worker graph를 시작한다.

```text
command line
-> Host config load / exact-schema parse / cross-field validation
-> normalized effective config와 application logging 준비
-> stop signal과 controller-owned control 연결
-> NrServer create / start / Gateway acquire
-> World storage, registries, adapters와 workers 조립
-> Publisher -> Pump -> Coordinator start
-> serving / health observation
-> stop request 또는 worker failure
-> World output drain / terminal ingress / Runtime shutdown
-> diagnostic owner와 control worker join
-> logger flush / process exit
```

Host process 하나는 하나의 Channel ID/name, listener endpoint와 독립 World state를 가진다. Local fleet 도구는 여러 Host process를 실행할 뿐 이 state를 하나의 World로 합치지 않는다.

## 책임과 제외 범위

이 문서는 다음을 설명한다.

- Host command line과 config schema의 caller-visible contract
- config group이 Runtime·World owner로 전달되는 방향
- process startup, serving, failure와 shutdown 순서
- application logging, Runtime diagnostics와 effective configuration의 ownership
- single Host와 local fleet의 실행 entrypoint
- Host 동작을 변경할 때 시작할 source와 contract tests

Worker 내부 A/B buffer와 tick handoff는 [World Server 실행 ownership과 fixed-step pipeline](runtime-ownership-and-tick-pipeline.md), gameplay rule은 [Authoritative Gameplay와 Round 계약](authoritative-gameplay-and-round-contract.md)이 담당한다. 이 문서는 benchmark 결과나 특정 workload의 운영 수치를 제공하지 않는다.

## Command line contract

[`WorldServerHostCommandLine`](../../src/PrivateServer.WorldServer.Host/WorldServerHostCommandLine.cpp)은 Host가 객체를 만들기 전에 option을 검증한다.

| 입력 | 의미 | 제약 |
| --- | --- | --- |
| `--config`와 config path | 단일 Channel Host JSON config | 필수이며 한 번만 지정 |
| controller-owned child-control context | parent와 stop/completion을 교환 | command/event handle을 함께 제공해야 하며 controller가 관리하는 context도 함께 필요 |

Unknown option, option 중복, 값 누락과 불완전한 child-control pair는 config나 Runtime을 열기 전에 거절한다. 일반 단일 Host 실행에는 config path만 필요하며 local scripts가 executable과 path resolution을 담당한다.

## Host config schema

[`WorldServerHostConfigSource`](../../src/PrivateServer.WorldServer.Host/WorldServerHostConfig.cpp)는 top-level object와 각 nested object가 정확히 허용된 key만 가지는지 확인한다. Schema name과 version이 일치해야 하며 unknown field, 누락 field, 잘못된 type과 fractional integer를 호환 가능한 값으로 추측하지 않는다.

| Config group | 주요 의미 | 전달되는 owner |
| --- | --- | --- |
| `channel` | Channel ID와 표시 이름 | Join baseline, logs와 Host identity |
| `logging` | application log minimum severity | `ApplicationLogger` |
| `network` | IPv4 listener, Runtime queue/mailbox와 payload-pool bounds | public `NrServerConfig` |
| `execution` | fixed-step schedule, catch-up, ingress/outbound storage와 shutdown bounds | World storage, Coordinator와 diagnostic writers |
| `simulation.physics` / `arena` | contact policy와 fixed Map Bounds | `WorldTickProcessor`, physics와 join baseline |
| `simulation.movement` | base/boost movement와 angular control | `WorldControlMovementSolver` |
| `simulation.growth` / `body` / `player` | growth dimensions, trail sampling과 Player spawn shape | tick processor, player body와 join/spawn config |
| `replication` | state cadence, command slack, spatial index/AOI와 Player ID allocation | join, command admission, AOI와 replication planners |
| `gameplay` | round, resource, boost cost와 Active Area rule | `WorldGameplayState`, phase와 committer |

Parse가 끝나면 [`WorldServerHostConfigSource::Validate`](../../src/PrivateServer.WorldServer.Host/WorldServerHostConfig.cpp)가 group 안의 range뿐 아니라 서로 연결된 값을 확인한다. 예를 들어 execution cadence와 join metadata, arena와 spawn, player shape와 growth, replication spatial rule, gameplay와 growth config가 서로 모순되지 않아야 한다.

Host는 validation을 통과한 config를 Runtime과 World 객체에 직접 전달할 typed config로 변환한다. Client의 [`channels.local.json`](../../src/PrivateServer.GameClient/Config/channels.local.json)은 Channel ID/name과 접속 endpoint만 소유하며 server config 전체를 복제하지 않는다.

## Configuration source와 effective configuration

Repository의 [`config/`](../../config/)에는 목적별 Host config와 local fleet manifest가 있다.

- `world-server-baseline.json`: 일반적인 single-Channel 기준 config
- `world-server-solo-debug.json`: 독립 실행과 수동 확인용 profile
- `world-server-channel-2.json`: 별도 Channel identity와 endpoint 예시
- `world-host-fleet.json`: 여러 Host process를 실행하는 local fleet 입력

Fleet manifest는 Host binary의 JSON schema가 아니다. [`run-world-fleet.ps1`](../../tools/run-world-fleet.ps1)이 manifest를 읽고 각 child에 개별 Host config를 전달한다. Host process는 자신의 config 한 개만 읽는다.

Config load와 validation 후 Host는 typed value를 canonical JSON으로 다시 serialize한다. 실행에 실제 적용된 값은 원본 파일의 formatting이나 key order가 아니라 이 normalized effective configuration으로 확인한다. `SerializeNormalized`는 스스로 validation을 수행하지 않으므로 `Load`가 성공한 값이나 caller가 명시적으로 검증한 값에만 사용한다.

## Startup과 composition

[`main.cpp`](../../src/PrivateServer.WorldServer.Host/main.cpp)는 다음 owner를 순서대로 준비한다.

1. command line parse와 config load/validation
2. diagnostic output owner와 normalized effective config
3. `ApplicationLogger`와 World log handle
4. console stop signal과 controller-managed child-control channel
5. [`WorldServerHostRunner`](../../src/PrivateServer.WorldServer.Host/WorldServerHostRunner.cpp)

Logger와 borrowed World log handle은 Runner가 만드는 Runtime/World owner보다 오래 산다. Logger가 시작된 뒤 Runner와 child-control에서 발생한 startup/shutdown 실패는 owner가 파괴되기 전에 같은 structured logging 경계로 기록할 수 있다. Command line, config, diagnostic output과 logger 준비 자체가 실패한 경우에는 stderr가 먼저 사용된다.

Runner는 config를 public `NrServerConfig`로 매핑해 `NrServer`를 create/start하고 server-bound `NrGateway`를 얻는다. 이후 다음 World graph를 조립한다.

```text
WorldExecutionStorage
|-- ingress / outbound double buffers
|-- event source and Runtime integration adapter
|-- session registry, entity manager and movement command store
|-- ingress consumer-owned gameplay state
|-- WorldIngressPump
|-- WorldDoubleBufferedTickCoordinator
`-- WorldOutboundPublisher
```

Workers는 Publisher, Pump, Coordinator 순서로 시작한다. 중간 startup 실패는 이미 시작한 worker를 역순으로 stop/join하고 Runtime admission을 닫는다. Worker가 시작된 뒤 Host main thread는 gameplay state를 직접 변경하지 않는다. Process stop signal, controller-managed control과 worker health를 관찰하고 Runtime snapshot을 주기적으로 수집해 diagnostic queue에 제출한다.

## Stop source와 shutdown

Host가 serving을 끝내는 source는 다음과 같다.

- Ctrl+C 또는 Ctrl+Break를 받은 `WorldServerHostStopSignal`이 stop을 요청
- controller parent의 child-control worker가 stop command를 수신하거나 control channel failure를 보고
- Publisher, Pump 또는 Coordinator가 예기치 않은 stop reason을 보고

Stop source가 달라도 World와 Runtime의 lifetime 회수는 [`WorldWorkerShutdown`](../../src/PrivateServer.WorldServer/WorldWorkerShutdown.h)이 소유한다.

```text
새 gameplay/output 생성 중단
-> 마지막 sealed outbound drain과 Publisher join
-> Pump / Coordinator terminal mode
-> NrServer stop request와 shutdown
-> remaining SessionAccepted / SessionClosed terminal consume
-> Pump / Coordinator join
-> diagnostic writer close/join
-> controller-managed child completion 통지와 join
-> terminal application record와 point-in-time logging health enqueue
-> ApplicationLogger destruction에서 accepted record drain / flush
```

Terminal ingress는 session lifecycle cleanup만 수행하고 packet을 새 gameplay input으로 적용하지 않는다. 한 shutdown 단계가 실패해도 가능한 worker join과 owner cleanup은 계속 시도하며 process result가 완료 여부를 반영한다.

## Application logging과 관측 경계

[`PrivateServer.ApplicationLogging`](../../src/PrivateServer.ApplicationLogging/)이 formatting, severity filter, sink fanout과 output lifetime을 소유한다. Host와 World는 structured event를 생성하지만 파일 sink를 직접 열고 닫지 않는다.

| Producer | 기록하는 의미 |
| --- | --- |
| [`WorldServerHostLog`](../../src/PrivateServer.WorldServer.Host/WorldServerHostLog.h) | application logging 시작, Runtime create/start/listening, World ready, stop 관측, worker failure와 shutdown outcome |
| [`WorldApplicationLogAdapter`](../../src/PrivateServer.WorldServer.Host/WorldApplicationLogAdapter.h) | join, session cleanup과 protocol close의 World application event를 log schema로 변환 |
| Runtime diagnostic sink | `NrServerConfig`의 별도 diagnostics output으로 NetworkRuntime diagnostic event를 기록 |
| diagnostic writers | Coordinator와 Runtime observation value의 파일 I/O를 simulation hot path와 분리 |

Application logging과 Runtime diagnostics는 서로 다른 output owner다. `WorldApplicationLogAdapter`는 World event를 application log schema로 변환하지만 Runtime diagnostic sink를 대신하지 않는다. Effective config와 diagnostics는 적용 설정과 실행 상태를 설명하는 관측 출력이며 public API나 gameplay authority가 아니다. 실행 중 Runtime snapshot 수집이나 queue 제출 실패는 기록·누적하며 simulation loop를 즉시 중단하지 않는다. Diagnostic writer도 running loop에서 synchronous file-I/O fallback을 만들지 않으며, 정상 stop 뒤 close/join 결과와 누적 collection failure가 최종 process result에 반영된다. `WorldServerHostLog::Complete`가 enqueue하는 logging health는 logger의 최종 drain/flush 이전 point-in-time 상태다.

## Local 실행 entrypoint

| 목적 | Entry point | 입력 경계 |
| --- | --- | --- |
| single Host 실행 | [`tools/run-world-host.ps1`](../../tools/run-world-host.ps1) | Host executable과 startup에서 Host가 검증하는 config 한 개 |
| local multi-Channel 실행 | [`tools/run-world-fleet.ps1`](../../tools/run-world-fleet.ps1) | fleet manifest가 참조하는 개별 Host config 집합 |
| benchmark fleet와 Observer 시각 검증 | [`tools/run-world-host-benchmark-fleet.ps1`](../../tools/run-world-host-benchmark-fleet.ps1) | 독립 benchmark config 두 개와 선택적 Godot Observer process |
| build와 실행 환경 확인 | [`tools/README.md`](../../tools/README.md) | Windows/Visual Studio toolchain, output과 troubleshooting |
| Client Channel 목록 | [`channels.local.json`](../../src/PrivateServer.GameClient/Config/channels.local.json) | Channel ID/name과 endpoint; Host internal config와 분리 |

Local fleet parent는 child process 시작/stop을 조정하지만 listener, Runtime Session, World registry와 round state는 각 Host가 독립적으로 소유한다. 현재 local Host/fleet script의 cleanup은 `Stop-Process`를 사용하므로 controller-managed child-control handshake나 graceful drain을 검증하는 entrypoint가 아니다. Benchmark fleet의 `-LaunchObservers`는 촬영·시각 검증용 Godot process와 read-only session을 추가하므로 해당 실행 artifact를 canonical 성능 비교에 사용하지 않는다.

## 변경 위치와 contract tests

| 변경 질문 | 구현 시작점 | Contract test |
| --- | --- | --- |
| JSON field나 cross-field invariant를 바꾸는가? | [`WorldServerHostConfig.h`](../../src/PrivateServer.WorldServer.Host/WorldServerHostConfig.h), [`WorldServerHostConfig.cpp`](../../src/PrivateServer.WorldServer.Host/WorldServerHostConfig.cpp), [`config/`](../../config/) | [`WorldServerHostConfigTests.cpp`](../../src/PrivateServer.WorldServer.Tests/WorldServerHostConfigTests.cpp)와 downstream config tests |
| command line 또는 controller-managed child contract를 바꾸는가? | [`WorldServerHostCommandLine.cpp`](../../src/PrivateServer.WorldServer.Host/WorldServerHostCommandLine.cpp), [`main.cpp`](../../src/PrivateServer.WorldServer.Host/main.cpp), child-control types | [`WorldServerHostCommandLineTests.cpp`](../../src/PrivateServer.WorldServer.Tests/WorldServerHostCommandLineTests.cpp), child-control tests |
| Runtime mapping이나 World graph를 바꾸는가? | [`WorldServerHostRunner.cpp`](../../src/PrivateServer.WorldServer.Host/WorldServerHostRunner.cpp), `WorldExecutionStorage`, worker startup/shutdown | Host config tests, worker startup/shutdown tests와 public loopback tests |
| stop source와 drain ordering을 바꾸는가? | `WorldServerHostStopSignal`, child-control worker, [`WorldWorkerShutdown.h`](../../src/PrivateServer.WorldServer/WorldWorkerShutdown.h) | [`WorldServerHostStopSignalTests.cpp`](../../src/PrivateServer.WorldServer.Tests/WorldServerHostStopSignalTests.cpp), child-control와 shutdown tests |
| log schema나 application event mapping을 바꾸는가? | [`WorldServerHostLog.cpp`](../../src/PrivateServer.WorldServer.Host/WorldServerHostLog.cpp), [`WorldApplicationLogAdapter.cpp`](../../src/PrivateServer.WorldServer.Host/WorldApplicationLogAdapter.cpp), ApplicationLogging public boundary | [`WorldServerHostLogTests.cpp`](../../src/PrivateServer.WorldServer.Tests/WorldServerHostLogTests.cpp), ApplicationLogging tests |
| single/fleet launcher behavior를 바꾸는가? | [`tools/run-world-host.ps1`](../../tools/run-world-host.ps1), [`tools/run-world-fleet.ps1`](../../tools/run-world-fleet.ps1), [`tools/run-world-host-benchmark-fleet.ps1`](../../tools/run-world-host-benchmark-fleet.ps1), fleet manifest | script validation과 Host command-line/config contracts |

## 관련 문서

- [프로젝트 Source Map](../project-source-map.md)
- [전체 시스템 아키텍처](../system-architecture.md)
- [World Server 실행 ownership과 fixed-step pipeline](runtime-ownership-and-tick-pipeline.md)
- [NetworkRuntime Public DLL 경계](../network-runtime/public-runtime-boundary.md)

## 지원 범위와 제약

- Host config는 exact schema를 사용하며 unknown field나 legacy execution mode를 묵시적으로 수용하지 않는다.
- 현재 listener config는 IPv4 endpoint를 사용한다.
- Host process 하나는 Channel 하나와 독립 World instance 하나를 소유한다.
- Local fleet는 여러 Host의 process lifecycle만 조정하며 dynamic discovery, matchmaking, shared World state나 seamless handoff를 제공하지 않는다.
- Graceful `ready -> stop -> stopped/error` handshake는 controller-managed 실행 계약이며 local fleet launcher의 cleanup 계약이 아니다.
- Application diagnostics는 runtime behavior를 관찰하는 별도 경계이며 gameplay state를 변경하는 입력이 아니다.
