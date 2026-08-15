# Private Server 시스템 아키텍처

> Document status: Reviewed
> Baseline: 1508dacf340e52cb4ec67e7e7a60d05755510553
> Last reviewed: 2026-08-12

## 핵심 답

Private Server는 Windows-native NetworkRuntime, Channel별 World Host, server-authoritative World와 thin Godot Game Client를 하나의 모노레포에서 구현한다.

서버의 실행 프로세스는 `PrivateServer.WorldServer.Host`다. `PrivateServer.WorldServer`는 별도 서버 프로세스가 아니라 Host에 link되는 static library이며, Host가 Channel configuration, NetworkRuntime server, World state와 worker lifetime을 조립한다.

```text
WorldServer.Host.exe
|-- WorldServer.lib
|-- NetworkRuntime.dll
|   `-- NetworkRuntime.Internal.lib
`-- ApplicationLogging.lib

Godot GameClient
`-- NetworkRuntime.Managed
    `-- P/Invoke
        `-- NetworkRuntime.CAbi.dll
            `-- NetworkRuntime.dll
```

Runtime은 transport와 connection lifetime을, World는 authoritative gameplay와 recipient 결정을, Client는 local prediction과 presentation을 소유한다. 어떤 계층도 다른 계층의 mutable state를 직접 공유하지 않는다.

## 책임과 제외 범위

이 문서는 다음 질문을 다룬다.

- Solution과 실행 파일의 build dependency는 어떻게 구성되는가?
- Server process에서 NetworkRuntime과 World의 lifetime을 누가 조립하는가?
- Socket input이 authoritative simulation을 거쳐 client presentation으로 돌아오는 경계는 무엇인가?
- Channel, Runtime Session, Player, World Entity와 client generation은 어떻게 구분되는가?
- Startup과 shutdown의 outer owner는 누구인가?

다음 내용은 subsystem 문서의 책임이다.

- Session Actor 내부의 pending I/O와 close/drain state
- World fixed-step phase와 A/B buffer ownership의 세부 순서
- Game Client의 disconnect/reconnect와 Godot node lifecycle
- capacity와 성능 결과

## Build topology

### Server와 Runtime

| Project | 산출물 | 의존 방향 | 역할 |
| --- | --- | --- | --- |
| `PrivateServer.NetworkRuntime.Internal` | static library | Windows/Winsock 구현과 internal utility | IOCP, listener, session actor, framing, queue와 pool 구현 |
| `PrivateServer.NetworkRuntime` | DLL과 import library | `NetworkRuntime.Internal` | public server/client runtime shell과 value contract |
| `PrivateServer.WorldServer` | static library | public `NetworkRuntime` | authoritative World domain, simulation, AOI와 replication |
| `PrivateServer.ApplicationLogging` | static library | application logging contract | Host process logging adapter |
| `PrivateServer.WorldServer.Host` | executable | `WorldServer`, public `NetworkRuntime`, `ApplicationLogging` | Channel별 composition root와 process lifecycle |

`PrivateServer.NetworkRuntime` DLL은 internal static library를 숨기고 staged public header를 제공한다. `WorldServer`와 Host는 IOCP, actor registry나 `OVERLAPPED` type에 직접 의존하지 않고 `NrServer`, `NrToWorldEvent`, `NrGateway`와 send-channel capability를 사용한다.

`WorldServer` project가 static library라는 점은 중요하다. `World instance`는 Host가 조립한 registry, entity manager, gameplay state, A/B storage와 worker의 논리적 집합이며, 별도 process나 하나의 global `World` object를 뜻하지 않는다.

### Client adapter chain

| Project | 경계 | 역할 |
| --- | --- | --- |
| `PrivateServer.NetworkRuntime.CAbi` | native DLL | public Runtime client를 opaque C handle과 POD contract로 투영 |
| `PrivateServer.NetworkRuntime.Managed` | Managed library | C ABI를 SafeHandle과 owning event value로 감쌈 |
| `PrivateServer.GameClient` | Godot C# application | Channel flow, session model, prediction, replica와 presentation |

Game Client는 Managed adapter를 project reference로 사용한다. Managed adapter는 C ABI entry point를 P/Invoke하고, C ABI DLL은 public NetworkRuntime DLL을 참조한다. Godot gameplay code는 native socket, IOCP와 Runtime internal header를 알지 않는다.

Root solution은 Runtime, C ABI, Managed adapter와 smoke, World, Host, native tests와 benchmark project를 함께 관리한다. Godot client와 client tests는 별도 client solution에서 Managed adapter와 함께 구성된다. 이 분리는 build entry를 나누지만 repository와 gameplay protocol source of truth를 나누지는 않는다.

## Runtime topology

서버의 정상 request/response 경로는 다음과 같다.

```text
TCP connection
-> NetworkRuntime listener와 IOCP
-> Runtime Session Actor
-> frame parse
-> owning NrToWorldEvent
-> World Ingress Pump
-> ingress A/B slot seal
-> World Coordinator
-> lifecycle, input과 authoritative fixed-step phase commit
-> AOI와 replication plan
-> outbound A/B slot seal
-> World Outbound Publisher
-> NrGateway
-> Runtime Session send path
-> TCP connection
```

### NetworkRuntime

NetworkRuntime은 다음 transport 책임을 소유한다.

- Winsock startup, listener, socket과 IOCP handle
- Runtime Session Key와 per-session actor lifetime
- Packet frame parse와 outbound frame ownership
- To-World owning event와 World-facing send capability
- Connection close, transport pressure와 server stop/shutdown

Completion worker는 World state를 직접 변경하지 않는다. Session Actor가 connection별 mutable I/O state를 직렬화하고, World로 넘기는 packet은 owning event lifetime을 가진다.

### World Host와 World Server

Host process가 public `NrServer`를 만들고 시작한 뒤 `NrGateway`, World execution storage, session/entity registry와 World worker를 조립한다. Host 하나는 configuration의 Channel 하나와 독립 World instance 하나를 소유한다.

World worker는 역할을 나눈다.

- Ingress Pump: To-World event를 ingress write slot으로 이동
- Coordinator: lifecycle과 input을 반영하고 authoritative simulation과 phase commit을 직렬화
- Outbound Publisher: sealed World record를 public Gateway에 제출

World는 player/entity binding, movement와 gameplay rule, AOI visible set, recipient 선택과 semantic packet을 소유한다. Socket framing이나 실제 send completion은 소유하지 않는다.

### Game Client

Native client Runtime은 connection worker와 caller-facing event queue를 소유한다. C ABI와 Managed adapter는 event를 opaque handle에서 Managed-owned value로 바꾸고, Godot main thread가 해당 event를 session model에 적용한다.

Client는 다음 local 책임만 가진다.

- Channel selection과 connection flow
- Server baseline의 local copy와 entity generation 확인
- Controlled entity prediction과 authoritative reconciliation
- Remote replica history와 presentation sampling
- Godot scene, HUD, effect와 result 화면

Collision result, resource 획득, score, round, spawn, death와 AOI membership은 server-authoritative state를 표현할 뿐 client가 결정하지 않는다.

## Composition과 lifecycle owner

[`WorldServerHostRunner`](../src/PrivateServer.WorldServer.Host/WorldServerHostRunner.cpp)가 server process의 outer composition root다.

Startup의 큰 순서는 다음과 같다.

1. Host configuration에서 Channel과 Runtime/World 설정을 읽는다.
2. `NrServer` graph를 만들고 listener를 시작한다.
3. Server-bound `NrGateway`를 발급받는다.
4. World A/B storage, registry, gameplay state와 adapter를 만든다.
5. Publisher, Pump와 Coordinator worker를 시작한다.
6. Host main thread가 stop condition과 runtime state를 관찰한다.

World state object는 Host scope가 소유하고 worker는 실행 중 reference만 사용한다. Worker startup 중 실패하면 이미 시작된 worker를 역순으로 stop/join한다.

Shutdown의 큰 순서는 다음과 같다.

```text
새 gameplay와 outbound 생성 중단
-> 마지막 sealed outbound drain
-> terminal ingress consume 진입
-> NetworkRuntime submission 차단과 shutdown
-> 남은 World lifecycle event consume
-> worker join
-> Host-owned World와 Runtime object 파괴
```

이 순서는 World가 새 output을 만드는 동안 Runtime admission이 먼저 사라지거나, worker가 reference하는 storage가 먼저 파괴되는 일을 막는다.

## Public boundary와 dependency rule

| 경계 | 이동하는 값 | 허용하지 않는 결합 |
| --- | --- | --- |
| Runtime -> World | `NrToWorldEvent`, Runtime Session Key, owning payload와 send capability | IOCP context, socket, actor pointer |
| World -> Runtime | `NrGateway` submit, semantic packet type와 payload | World entity storage, AOI planner, gameplay object |
| Native Runtime -> Managed | opaque client/event handle, POD status와 payload view | C++ object layout과 Win32 handle |
| Managed -> Game Client | owning event payload, status와 endpoint value | native event lifetime과 Runtime internal type |
| Session model -> Godot | prediction/replica snapshot과 flow observation | transport worker가 직접 변경하는 scene node |

World와 Game Client가 공유하는 것은 wire protocol의 의미다. Runtime은 packet type과 payload를 운반하고 framing/lifetime을 소유하지만 gameplay packet field를 authoritative domain state로 해석하지 않는다.

## Identity map

| Identity 또는 capability | 의미 | Owner와 lifetime |
| --- | --- | --- |
| Channel ID | Host가 실행하는 gameplay instance identity | Host configuration과 해당 World instance |
| Runtime Session Key | 한 transport connection identity | NetworkRuntime connection lifetime |
| Player ID | Channel 안의 joined participant identity | World session/player binding |
| World Entity Key | Entity ID와 generation의 조합 | World entity lifecycle |
| Transport Generation | client reconnect별 local generation | `RemoteGameplaySession` |
| `NrSessionSendChannel` | 특정 Runtime Session으로 보내는 capability | Runtime control object를 안전하게 참조하며 identity나 close 권한은 제공하지 않음 |

Runtime Session Key를 Player ID로 사용하거나 Entity ID만으로 respawn 뒤 객체를 식별하면 서로 다른 lifetime을 섞게 된다. 각 boundary는 필요한 identity를 명시적으로 변환하고 generation을 확인한다.

## Failure boundary

| 실패 위치 | 책임 owner | 외부로 보이는 결과 |
| --- | --- | --- |
| connection, frame 또는 pending I/O | NetworkRuntime | status, disconnect reason과 SessionClosed event |
| join prepare 또는 World capacity | World | 준비한 state rollback과 session close 요청 |
| simulation, outbound append 또는 worker | World Host | controlled stop과 worker shutdown |
| Gateway admission | NetworkRuntime | submit failure 또는 recipient별 admission 결과 |
| packet ordering, decode 또는 Channel mismatch | Game Client session | fault state와 disconnect 요청 |
| scene teardown | Game Client scene/session | transport shutdown 요청과 owned handle 회수 |

한 계층의 failure가 다른 계층의 mutable object rollback을 직접 호출하지 않는다. 각 owner가 자신의 state를 정리하고 public event, status 또는 capability 결과로 다음 계층에 알린다.

## 관련 구현과 테스트

| 독자 질문 | 관련 구현 | 관련 테스트 |
| --- | --- | --- |
| Server executable과 library dependency는 어떻게 구성되는가? | [`PrivateServer.sln`](../PrivateServer.sln), [`PrivateServer.WorldServer.Host.vcxproj`](../src/PrivateServer.WorldServer.Host/PrivateServer.WorldServer.Host.vcxproj) | solution과 project reference configuration |
| Host는 Runtime과 World lifetime을 어디서 조립하는가? | [`WorldServerHostRunner.cpp`](../src/PrivateServer.WorldServer.Host/WorldServerHostRunner.cpp), [`WorldWorkerStartup.h`](../src/PrivateServer.WorldServer/WorldWorkerStartup.h) | [`WorldWorkerStartupTests.cpp`](../src/PrivateServer.WorldServer.Tests/WorldWorkerStartupTests.cpp) |
| Runtime 내부 구현은 public consumer와 어떻게 분리되는가? | [`PrivateServer.NetworkRuntime.vcxproj`](../src/PrivateServer.NetworkRuntime/PrivateServer.NetworkRuntime.vcxproj), [`NrServer.h`](../src/PrivateServer.NetworkRuntime/NrServer.h) | [`NrServerLifecycleTests.cpp`](../src/PrivateServer.NetworkRuntime.PublicTests/NrServerLifecycleTests.cpp) |
| Runtime event가 World tick과 outbound로 어떻게 연결되는가? | [`WorldDoubleBufferedTickCoordinator.h`](../src/PrivateServer.WorldServer/WorldDoubleBufferedTickCoordinator.h), [`WorldOutboundPublisher.h`](../src/PrivateServer.WorldServer/WorldOutboundPublisher.h) | [`WorldPublicLoopbackTests.cpp`](../src/PrivateServer.WorldServer.Tests/WorldPublicLoopbackTests.cpp) |
| Managed adapter가 native client를 어떻게 감싸는가? | [`psnr_cabi.cpp`](../src/PrivateServer.NetworkRuntime.CAbi/psnr_cabi.cpp), [`NetworkRuntimeClient.cs`](../src/PrivateServer.NetworkRuntime.Managed/NetworkRuntimeClient.cs) | [`Managed Smoke`](../src/PrivateServer.NetworkRuntime.Managed.Smoke/Program.cs) |
| Godot main thread는 session state를 어디서 presentation에 반영하는가? | [`RemoteGameplayScene.cs`](../src/PrivateServer.GameClient/Gameplay/Presentation/RemoteGameplayScene.cs), [`RemoteGameplaySession.cs`](../src/PrivateServer.GameClient/Gameplay/Remote/RemoteGameplaySession.cs) | [`RemoteGameplaySessionTests.cs`](../src/PrivateServer.GameClient.Tests/RemoteGameplaySessionTests.cs) |
| Stop은 Runtime과 World worker를 어떤 순서로 회수하는가? | [`WorldWorkerShutdown.h`](../src/PrivateServer.WorldServer/WorldWorkerShutdown.h), [`NrServer.cpp`](../src/PrivateServer.NetworkRuntime/NrServer.cpp) | [`WorldWorkerShutdownTests.cpp`](../src/PrivateServer.WorldServer.Tests/WorldWorkerShutdownTests.cpp) |

## 상세 문서

- [NetworkRuntime Public DLL 경계](network-runtime/public-runtime-boundary.md)
- [Session Actor ownership과 I/O lifetime](network-runtime/session-actor-ownership-and-io-lifetime.md)
- [World Host 설정과 Process Lifecycle](world-server/host-configuration-and-process-lifecycle.md)
- [World Server 실행 ownership과 fixed-step pipeline](world-server/runtime-ownership-and-tick-pipeline.md)
- [Gameplay Protocol Reference](world-server/gameplay-protocol-reference.md)
- [Authoritative Gameplay와 Round 계약](world-server/authoritative-gameplay-and-round-contract.md)
- [AOI, Active Area와 Replication 계약](world-server/aoi-active-area-and-replication.md)
- [Game Client main-thread session과 presentation lifecycle](game-client/main-thread-session-and-presentation-lifecycle.md)

## 지원 범위와 제약

- 현재 서버 배치는 Host process별 Channel/World instance다. 여러 Channel이 하나의 mutable World를 공유하지 않는다.
- Production MMORPG 규모의 seamless World, region partition과 distributed World transaction은 현재 범위가 아니다.
- Channel directory는 local configuration이며 service discovery, health routing이나 matchmaking을 제공하지 않는다.
- Client는 thin presentation layer이며 authoritative gameplay fallback을 제공하지 않는다.
- 이 문서는 특정 workload capacity나 성능을 보장하지 않는다.
