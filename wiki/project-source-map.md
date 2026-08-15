# 프로젝트 Source Map

> Document status: Reviewed
> Baseline: c0bd3a8e5f1861c6dc1321381b6c58ca7a374030
> Last reviewed: 2026-08-16

## 핵심 답

Private Server의 제품 실행 경계는 Channel 하나를 소유하는 `PrivateServer.WorldServer.Host`와 이를 Player 또는 read-only Observer mode로 시각화하는 Godot `PrivateServer.GameClient`다. Host는 public NetworkRuntime, World Server와 application logging을 조립하고, Game Client는 Managed/C ABI adapter를 통해 native client Runtime을 사용한다.

```text
Server
WorldServer.Host.exe
|-- WorldServer.lib
|-- NetworkRuntime.dll
|   `-- NetworkRuntime.Internal.lib
`-- ApplicationLogging.lib

Client
GameClient (Godot C#)
`-- NetworkRuntime.Managed
    `-- NetworkRuntime.CAbi.dll
        `-- NetworkRuntime.dll
```

처음 보는 개발자는 변경하려는 책임의 owner를 먼저 고른 뒤 해당 project, public 또는 integration boundary, 같은 책임을 검증하는 test project 순서로 좁히면 된다. IOCP와 socket lifetime은 NetworkRuntime Internal, gameplay authority는 World Server, process 조립과 설정은 Host, 화면과 local prediction은 Game Client에서 시작한다.

## 책임과 제외 범위

이 문서는 다음 질문에 답한다.

- 어떤 solution과 project가 어떤 산출물을 만드는가?
- 외부 caller가 사용할 수 있는 API와 내부 구현 경계는 어디인가?
- transport frame과 gameplay semantic payload의 source of truth는 어디인가?
- 변경 목적에 따라 어느 구현과 contract test에서 조사를 시작해야 하는가?
- build, local execution과 configuration 안내는 어디에 있는가?

각 subsystem 내부 알고리즘, packet별 wire field, session actor state transition과 fixed-step phase 세부 순서는 별도 문서가 담당한다. Raw benchmark 결과와 운영 artifact도 이 문서의 범위가 아니다.

## 저장소 지도

| 경로 | 소유하는 내용 | 여기서 시작하는 경우 |
| --- | --- | --- |
| [`src/`](../src/) | 제품 code, adapter, executable과 contract test | 동작이나 public contract를 변경할 때 |
| [`config/`](../config/) | World Host와 local fleet의 checked-in configuration | Channel, endpoint 또는 Host 실행 구성을 확인할 때 |
| [`tools/`](../tools/) | build, Host/fleet 실행과 검증용 wrapper | 로컬 build·실행 진입점을 찾을 때 |
| [`wiki/`](./) | 현재 code와 test 기준의 공개 기술 설명 | 책임, interface, ownership과 runtime scenario를 이해할 때 |
| [`docs/`](../docs/) | ADR와 반복 적용되는 implementation convention | 설계 이유나 공통 경계 규칙을 확인할 때 |

저장소의 root solution은 native project와 Managed adapter/smoke를 함께 묶는 [`PrivateServer.sln`](../PrivateServer.sln)이다. Godot application과 Managed adapter의 개발 진입점은 [`PrivateServer.GameClient.sln`](../src/PrivateServer.GameClient/PrivateServer.GameClient.sln)이다.

## Project와 산출물

### 제품 실행 경계

| Project | 산출물 | 책임 | 주요 의존 |
| --- | --- | --- | --- |
| [`PrivateServer.WorldServer.Host`](../src/PrivateServer.WorldServer.Host/) | executable | Channel configuration, Runtime·World·worker 조립, process startup/shutdown | WorldServer, public NetworkRuntime, ApplicationLogging |
| [`PrivateServer.WorldServer`](../src/PrivateServer.WorldServer/) | static library | session role admission, authoritative gameplay, entity/session binding, fixed-step simulation, AOI와 replication | public NetworkRuntime integration boundary, project에 직접 compile되는 vendored Box2D C source |
| [`PrivateServer.NetworkRuntime`](../src/PrivateServer.NetworkRuntime/) | DLL과 import library | server/client public shell, event, capability, status와 snapshot | NetworkRuntime.Internal |
| [`PrivateServer.NetworkRuntime.Internal`](../src/PrivateServer.NetworkRuntime.Internal/) | static library | IOCP, Winsock, listener, session actor, framing, queue·pool과 diagnostics 구현 | Windows networking/runtime |
| [`PrivateServer.ApplicationLogging`](../src/PrivateServer.ApplicationLogging/) | static library | process 공통 structured application logging | vendored spdlog와 nlohmann/json package |
| [`PrivateServer.GameClient`](../src/PrivateServer.GameClient/) | Godot C# application | Channel flow, Player/Observer session model, prediction, replica와 overview presentation | NetworkRuntime.Managed |

`PrivateServer.WorldServer`는 별도 실행 process가 아니다. Host에 link된 뒤 Host-owned World instance를 구성한다. 반대로 `PrivateServer.NetworkRuntime`은 DLL public boundary이며, 제품 consumer는 `PrivateServer.NetworkRuntime.Internal`을 직접 사용하지 않는다.

### Client adapter 경계

| Project | 경계 | 책임 |
| --- | --- | --- |
| [`PrivateServer.NetworkRuntime.CAbi`](../src/PrivateServer.NetworkRuntime.CAbi/) | native DLL | `NrClient`를 opaque C handle, POD value와 explicit destroy 함수로 투영 |
| [`PrivateServer.NetworkRuntime.Managed`](../src/PrivateServer.NetworkRuntime.Managed/) | .NET library | C ABI handle을 `SafeHandle`과 owning Managed event value로 변환 |
| [`PrivateServer.GameClient`](../src/PrivateServer.GameClient/) | Godot main thread | Managed event를 session, prediction, replica와 scene state에 반영 |

C ABI는 C++ object layout을 노출하지 않고, Managed adapter는 native event payload를 event handle이 파괴되기 전에 Managed-owned memory로 복사한다. Godot code는 native pointer, socket과 IOCP type을 보관하지 않는다.

### 검증 경계

| Project | 확인하는 계약 |
| --- | --- |
| [`PrivateServer.NetworkRuntime.PublicTests`](../src/PrivateServer.NetworkRuntime.PublicTests/) | staged public header와 DLL/import library를 사용하는 caller-visible Runtime 동작 |
| [`PrivateServer.NetworkRuntime.InternalTests`](../src/PrivateServer.NetworkRuntime.InternalTests/) | IOCP adapter, actor, parser, queue, pool과 internal lifecycle seam |
| [`PrivateServer.WorldServer.Tests`](../src/PrivateServer.WorldServer.Tests/) | World protocol, lifecycle, simulation, AOI, replication과 Host integration |
| [`PrivateServer.GameClient.Tests`](../src/PrivateServer.GameClient.Tests/) | Managed session, protocol mirror, prediction, replica와 presentation projection |
| [`PrivateServer.ApplicationLogging.Tests`](../src/PrivateServer.ApplicationLogging.Tests/) | logging value, formatting, sink와 output lifecycle |
| [`PrivateServer.NetworkRuntime.Smoke`](../src/PrivateServer.NetworkRuntime.Smoke/) | public native Runtime을 통한 process-level connection 흐름 |
| [`PrivateServer.NetworkRuntime.Managed.Smoke`](../src/PrivateServer.NetworkRuntime.Managed.Smoke/) | Managed adapter를 통한 client connection 흐름 |
| [`PrivateServer.NetworkRuntime.Benchmark`](../src/PrivateServer.NetworkRuntime.Benchmark/) | public Runtime과 World Host를 사용하는 repeatable workload controller |

Test project는 해당 계약을 찾는 navigation source다. Test source의 존재만으로 특정 baseline의 실행 성공이나 성능 결과를 뜻하지 않는다.

## Public API 지도

### Server-facing Runtime

| Public type | Caller | 제공하는 계약 | 구현·contract test |
| --- | --- | --- | --- |
| `NrServer` | World Host | Runtime graph 생성, start/stop/shutdown, To-World event drain, session close 요청과 snapshot capture | [`NrServer.h`](../src/PrivateServer.NetworkRuntime/NrServer.h), [`NrServerLifecycleTests.cpp`](../src/PrivateServer.NetworkRuntime.PublicTests/NrServerLifecycleTests.cpp) |
| `NrToWorldEvent` | World ingress adapter | accepted session, received packet와 closed session을 move-only owning event로 전달 | [`NrToWorldEvent.h`](../src/PrivateServer.NetworkRuntime/NrToWorldEvent.h), [`NrToWorldEventTests.cpp`](../src/PrivateServer.NetworkRuntime.PublicTests/NrToWorldEventTests.cpp) |
| `NrGateway` | World outbound publisher | 이미 결정된 recipient에게 semantic packet을 제출 | [`NrGateway.h`](../src/PrivateServer.NetworkRuntime/NrGateway.h), [`NrGatewayTests.cpp`](../src/PrivateServer.NetworkRuntime.PublicTests/NrGatewayTests.cpp) |
| `NrSessionSendChannel` | World session view | 한 Runtime Session에 대한 copyable send-only capability | [`NrSessionSendChannel.h`](../src/PrivateServer.NetworkRuntime/NrSessionSendChannel.h), [`NrSessionSendChannelTests.cpp`](../src/PrivateServer.NetworkRuntime.PublicTests/NrSessionSendChannelTests.cpp) |
| `NrServerSnapshot` | Host와 관측 도구 | capture 시점의 lifecycle과 pressure observation value | [`NrServerSnapshot.h`](../src/PrivateServer.NetworkRuntime/NrServerSnapshot.h), [`NrServerSnapshotTests.cpp`](../src/PrivateServer.NetworkRuntime.PublicTests/NrServerSnapshotTests.cpp) |

`NrGateway::Submit`과 `SubmitMany`의 성공은 Runtime-owned send work의 admission을 뜻한다. Socket completion이나 client 수신 완료를 뜻하지 않는다. `NrToWorldEvent`에서 얻은 payload view는 event owner가 살아 있는 동안만 유효하므로 World가 보관할 값은 event lifetime 안에서 World-owned command나 storage로 변환한다.

### Client-facing Runtime

| Public type 또는 경계 | Caller | 제공하는 계약 | 구현·contract test |
| --- | --- | --- | --- |
| `NrClient` | native client 또는 C ABI | create, connect/disconnect/shutdown, semantic packet send, event drain과 snapshot capture | [`NrClient.h`](../src/PrivateServer.NetworkRuntime/NrClient.h), [`NrClientTests.cpp`](../src/PrivateServer.NetworkRuntime.PublicTests/NrClientTests.cpp) |
| `NrClientEvent` | native client 또는 C ABI | connected, connection failed, packet received와 disconnected event의 owning lifetime | [`NrClientEvent.h`](../src/PrivateServer.NetworkRuntime/NrClientEvent.h), [`NrClientEventTests.cpp`](../src/PrivateServer.NetworkRuntime.PublicTests/NrClientEventTests.cpp) |
| `psnr_cabi` | Managed adapter | opaque client/event handle, POD status·endpoint·snapshot과 explicit destroy API | [`psnr_cabi.h`](../src/PrivateServer.NetworkRuntime.CAbi/psnr_cabi.h), [`psnr_cabi.cpp`](../src/PrivateServer.NetworkRuntime.CAbi/psnr_cabi.cpp) |
| `NetworkRuntimeClient` | Game Client | `SafeHandle` owner, Managed status/snapshot와 owning `NetworkRuntimeEvent` | [`NetworkRuntimeClient.cs`](../src/PrivateServer.NetworkRuntime.Managed/NetworkRuntimeClient.cs), [`Managed Smoke`](../src/PrivateServer.NetworkRuntime.Managed.Smoke/Program.cs) |

Public API의 세부 lifetime과 dependency rule은 [NetworkRuntime Public DLL 경계](network-runtime/public-runtime-boundary.md)를 따른다. Session actor와 pending I/O의 내부 수명은 [Session Actor ownership과 I/O lifetime](network-runtime/session-actor-ownership-and-io-lifetime.md)에서 설명한다.

## Gameplay protocol source of truth

Transport와 gameplay protocol은 서로 다른 책임이다.

```text
NetworkRuntime transport frame
|-- packet length / packet type / transport version / flags
`-- semantic payload
    `-- World gameplay protocol이 encode/decode
```

- [`WorldPacketTypes.h`](../src/PrivateServer.WorldServer/WorldPacketTypes.h)는 C2S/S2C packet catalog와 WorldIngress routing catalog를 소유한다.
- 각 [`World protocol header`](../src/PrivateServer.WorldServer/)는 `ObserveWorldRequest`/`ObserverReady`를 포함한 versioned packet value, wire offset, payload 크기, encode/decode와 field validation을 소유한다.
- [`WorldProtocolWireCodec.h`](../src/PrivateServer.WorldServer/WorldProtocolWireCodec.h)는 fixed-width little-endian primitive만 제공한다.
- [`WorldIngressPacketRouter.cpp`](../src/PrivateServer.WorldServer/WorldIngressPacketRouter.cpp)는 packet type과 semantic decoder/admission 경계를 연결한다.
- [`Gameplay/Protocol`](../src/PrivateServer.GameClient/Gameplay/Protocol/)은 Client가 소비하는 C# wire mirror다.
- [`WorldProtocolTests.cpp`](../src/PrivateServer.WorldServer.Tests/WorldProtocolTests.cpp)와 Game Client의 version별 protocol tests는 양쪽 codec의 field, invalid input과 version behavior를 추적하는 계약이다.

NetworkRuntime은 gameplay payload field를 해석하지 않는다. World protocol이나 simulation API에 `NrStatus`, `NrPacketType` 같은 Runtime type을 퍼뜨리지 않고 Runtime integration adapter에서만 packet type, session identity와 failure 의미를 변환한다.

## 변경 목적별 시작점

| 변경하려는 책임 | 첫 구현 진입점 | 함께 확인할 경계 | 대표 contract test |
| --- | --- | --- | --- |
| Server lifecycle 또는 public config | [`NrServer.h`](../src/PrivateServer.NetworkRuntime/NrServer.h), [`NrServer.cpp`](../src/PrivateServer.NetworkRuntime/NrServer.cpp) | public/internal DLL seam과 Host startup/shutdown | [`NrServerLifecycleTests.cpp`](../src/PrivateServer.NetworkRuntime.PublicTests/NrServerLifecycleTests.cpp) |
| Accept/recv/send 또는 pending I/O | [`PrivateServer.NetworkRuntime.Internal`](../src/PrivateServer.NetworkRuntime.Internal/)의 `NrListener`, `NrSessionIoActor`와 I/O context | actor mailbox single-consumer, pending I/O와 close/drain lifetime | [`PrivateServer.NetworkRuntime.InternalTests`](../src/PrivateServer.NetworkRuntime.InternalTests/)의 대응 `Nr*Tests.cpp` |
| Framing, recv buffer, queue 또는 memory pool | Internal의 `NrPacketParser`, `NrRecvBuffer`, `NrBoundedMpscQueue`, `NrMemoryPool` | transport frame과 allocation/lifetime invariant | 각 이름에 대응하는 InternalTests |
| World로 들어오는 session/gameplay event | [`WorldIngressEventConsumer.cpp`](../src/PrivateServer.WorldServer/WorldIngressEventConsumer.cpp), [`WorldIngressPacketRouter.cpp`](../src/PrivateServer.WorldServer/WorldIngressPacketRouter.cpp) | Runtime event lifetime, Player/Observer role, session binding과 command admission | [`WorldIngressEventConsumerTests.cpp`](../src/PrivateServer.WorldServer.Tests/WorldIngressEventConsumerTests.cpp), [`WorldSessionRegistryTests.cpp`](../src/PrivateServer.WorldServer.Tests/WorldSessionRegistryTests.cpp), [`WorldIngressPacketRouterTests.cpp`](../src/PrivateServer.WorldServer.Tests/WorldIngressPacketRouterTests.cpp) |
| Gameplay packet 또는 wire field | [`WorldPacketTypes.h`](../src/PrivateServer.WorldServer/WorldPacketTypes.h)와 해당 C++ packet header | C# protocol mirror, packet ordering과 version compatibility | [`WorldProtocolTests.cpp`](../src/PrivateServer.WorldServer.Tests/WorldProtocolTests.cpp)와 version별 Game Client protocol tests |
| Fixed-step movement·physics·gameplay rule | [`WorldDoubleBufferedTickCoordinator.h`](../src/PrivateServer.WorldServer/WorldDoubleBufferedTickCoordinator.h)와 해당 phase/solver | immutable tick input, typed result와 Coordinator-owned commit | [`WorldDoubleBufferedTickCoordinatorTests.cpp`](../src/PrivateServer.WorldServer.Tests/WorldDoubleBufferedTickCoordinatorTests.cpp)와 대응 phase/solver tests |
| Entity lifetime, AOI 또는 replication | World entity registry, spatial index, `WorldAoiPlanner`와 replication planner | Entity Key generation, visible set과 recipient ownership | [`WorldAoiPlannerTests.cpp`](../src/PrivateServer.WorldServer.Tests/WorldAoiPlannerTests.cpp)와 replication tests |
| World outbound 제출 | [`WorldOutboundPublisher.h`](../src/PrivateServer.WorldServer/WorldOutboundPublisher.h) | sealed outbound slot, `NrGateway` admission과 record ordering | Publisher, outbound buffer와 [`WorldPublicLoopbackTests.cpp`](../src/PrivateServer.WorldServer.Tests/WorldPublicLoopbackTests.cpp) |
| Host composition, Channel 또는 실행 설정 | [`WorldServerHostRunner.cpp`](../src/PrivateServer.WorldServer.Host/WorldServerHostRunner.cpp), [`WorldServerHostConfig.cpp`](../src/PrivateServer.WorldServer.Host/WorldServerHostConfig.cpp) | config validation, worker startup/rollback과 shutdown ordering | Host config와 World worker startup/shutdown tests |
| Native Client 또는 adapter ABI | [`NrClient.h`](../src/PrivateServer.NetworkRuntime/NrClient.h), [`psnr_cabi.h`](../src/PrivateServer.NetworkRuntime.CAbi/psnr_cabi.h), [`NetworkRuntimeClient.cs`](../src/PrivateServer.NetworkRuntime.Managed/NetworkRuntimeClient.cs) | opaque handle, event payload copy와 dispose lifetime | Runtime PublicTests와 [`Managed Smoke`](../src/PrivateServer.NetworkRuntime.Managed.Smoke/Program.cs) |
| Client session, prediction 또는 presentation | [`RemoteGameplaySession.cs`](../src/PrivateServer.GameClient/Gameplay/Remote/RemoteGameplaySession.cs), [`RemoteGameplayScene.cs`](../src/PrivateServer.GameClient/Gameplay/Presentation/RemoteGameplayScene.cs), [`WorldOverviewPresentation.cs`](../src/PrivateServer.GameClient/Gameplay/Presentation/WorldOverviewPresentation.cs) | Player/Observer mode, transport generation, main-thread mutation과 authoritative reconciliation | [`PrivateServer.GameClient.Tests`](../src/PrivateServer.GameClient.Tests/)의 대응 launch option/session/prediction/presentation tests |
| Application log schema 또는 sink | [`ApplicationLogger.h`](../src/PrivateServer.ApplicationLogging/ApplicationLogger.h), Host의 `WorldApplicationLogAdapter` | producer context, formatting, fanout과 output lifetime | [`ApplicationLoggerTests.cpp`](../src/PrivateServer.ApplicationLogging.Tests/ApplicationLoggerTests.cpp)와 대응 logging tests |

World 실행 ownership과 phase 순서는 [World Server 실행 ownership과 fixed-step pipeline](world-server/runtime-ownership-and-tick-pipeline.md), Client main-thread mutation과 teardown은 [Main thread session과 presentation lifecycle](game-client/main-thread-session-and-presentation-lifecycle.md), 전체 연결 흐름은 [End-to-end 게임 사이클](end-to-end-game-cycle.md)에서 이어서 확인할 수 있다.

## Configuration, build와 local execution

- [`config/`](../config/)의 Host configuration은 listener endpoint, Channel identity와 World execution/gameplay 설정을 제공한다.
- Game Client의 local Channel 목록은 [`channels.local.json`](../src/PrivateServer.GameClient/Config/channels.local.json)이며 server config path나 live discovery를 포함하지 않는다.
- [`tools/build.ps1`](../tools/build.ps1)은 일상적인 Server/Client build entrypoint다.
- [`tools/run-world-host.ps1`](../tools/run-world-host.ps1)과 [`tools/run-world-fleet.ps1`](../tools/run-world-fleet.ps1)은 단일 Host와 local multi-Channel 실행 entrypoint다.
- [`tools/run-world-host-benchmark-fleet.ps1`](../tools/run-world-host-benchmark-fleet.ps1)의 `-LaunchObservers`는 두 Channel workload를 Godot Observer 창으로 시각 검증하는 비정규 benchmark capture entrypoint다.
- 필요한 toolchain, command, output 위치와 문제 해결은 [`tools/README.md`](../tools/README.md)가 소유한다.

Configuration 값은 Host composition에서 검증한 뒤 Runtime config와 World-owned config로 나뉜다. Game Client의 Channel directory는 접속에 필요한 Channel ID, 표시 이름과 endpoint만 소유하며 server의 internal configuration을 복제하지 않는다.

## 변경 시 지켜야 할 경계

- NetworkRuntime public consumer는 staged public header와 DLL/import library만 사용한다.
- Runtime internal type을 WorldServer, Host 또는 Game Client API에 노출하지 않는다.
- World protocol, domain과 simulation은 World-owned fixed-width value와 error를 사용한다.
- Runtime integration adapter만 Runtime Session Key, packet type, payload lifetime과 World-owned value를 변환한다.
- Mutable World state는 Coordinator-owned commit 경계를 통하지 않고 worker나 completion thread에서 직접 변경하지 않는다.
- Godot scene state는 main thread가 변경하며 transport worker가 scene node를 직접 만지지 않는다.
- ID와 generation을 함께 사용하는 identity를 단순 ID로 축약해 서로 다른 lifetime의 객체를 혼동하지 않는다.

## 관련 설계

- [전체 시스템 아키텍처](system-architecture.md)
- [NetworkRuntime Public DLL 경계](network-runtime/public-runtime-boundary.md)
- [World Server와 NetworkRuntime 경계 규칙](../docs/design/conventions/world-server-runtime-boundary.md)
- [DLL 경계 규칙](../docs/design/conventions/dll-boundary.md)
- [Gameplay protocol reference](world-server/gameplay-protocol-reference.md)

## 지원 범위와 제약

- 공식 native build와 runtime 검증 환경은 Windows와 Visual Studio solution이다.
- Host process 하나는 Channel 하나와 독립 World instance 하나를 소유한다.
- Channel directory와 fleet orchestration은 local configuration이다.
- Account, persistence, dynamic service discovery, seamless migration과 cloud orchestration은 현재 제품 경계에 포함하지 않는다.
