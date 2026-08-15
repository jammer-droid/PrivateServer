# End-to-end 게임 사이클

> Document status: Reviewed
> Baseline: c0bd3a8e5f1861c6dc1321381b6c58ca7a374030
> Last reviewed: 2026-08-16

## 핵심 답

Private Server의 한 게임 사이클은 local Channel 선택에서 시작해 native connection과 World session role admission을 거친다. Player Session은 transactional join, fixed-step authoritative simulation과 AOI replication에 참여하고, Observer Session은 Channel-wide overview를 read-only로 표현한다. 두 경로 모두 RoundResult와 session cleanup으로 끝난다.

```text
Local Channel Directory
-> Channel 선택
-> native transport connect
-> Player: JoinWorldRequest / WorldReady / first time sync / Playing
   또는 Observer: ObserveWorldRequest / ObserverReady / Observing
-> World authoritative fixed-step commit
-> Player AOI·controlled state / Observer WorldOverview
-> RoundResult
-> client local result commit과 disconnect
-> World session/entity cleanup
-> 사용자의 복귀 선택
-> ChannelSelect
```

Client prediction은 화면 반응을 만들지만 gameplay 결과를 결정하지 않는다. Join, movement, resource, score, death, respawn, AOI, round와 winner는 World가 commit하고 Client는 authoritative packet을 local state와 presentation으로 투영한다.

## 책임과 제외 범위

이 문서는 다음 질문을 다룬다.

- Channel 선택이 어느 Host endpoint와 World instance로 연결되는가?
- Runtime Session은 언제 World Player와 Entity에 binding되는가?
- Baseline이 끝나고 movement가 열리는 경계는 무엇인가?
- Input이 authoritative state와 AOI-scoped replication으로 바뀌는 순서는 무엇인가?
- RoundResult 뒤 client와 World는 무엇을 정리하며 다음 연결은 언제 시작되는가?
- Player와 Observer admission, authority와 replication 경계는 어떻게 다른가?

다음 내용은 subsystem 문서의 책임이다.

- IOCP completion과 pending I/O context의 내부 수명
- World A/B buffer와 phase commit의 전체 구현 순서
- Client node와 transient effect의 세부 presentation lifecycle
- capacity와 성능 결과

## 참여 identity와 binding

게임 사이클에는 서로 다른 lifetime의 identity가 함께 등장한다.

| Identity | 의미 | 생성·회수 시점 |
| --- | --- | --- |
| Channel ID | 선택한 Host/World gameplay instance | Host configuration으로 고정, Client가 WorldReady에서 확인 |
| Runtime Session Key | TCP connection | NetworkRuntime accept에서 생성, connection close에서 회수 |
| World Session Role | Connected, Player 또는 Observer 중 하나인 participation 권한 | SessionAccepted에서 Connected, 성공한 join/observe admission에서 확정 |
| Player ID | Channel 안의 joined participant | Player join commit에서만 binding, SessionClosed에서 해제 |
| World Entity Key | entity ID와 generation | Player/entity spawn에서 생성, remove/cleanup에서 회수 |
| Transport Generation | Client의 local connection attempt generation | connect마다 전진, disconnect에서 generation-local state 정리 |

`SessionAccepted`만 받은 World session은 Connected role이며 아직 Player나 Observer가 아니다. `JoinWorldRequest`가 검증되고 baseline outbound가 준비된 뒤에야 Player ID와 controlled World Entity Key를 binding한다. `ObserveWorldRequest`는 같은 Connected 상태에서만 Observer role을 확정하지만 Player/Entity binding을 만들지 않는다.

## Channel 선택과 transport 연결

[`RemoteGameplayScene`](../src/PrivateServer.GameClient/Gameplay/Presentation/RemoteGameplayScene.cs)은 local Channel Directory를 읽어 Channel ID, 표시 이름과 IPv4 endpoint를 UI에 제공한다. Directory는 server config path, live population이나 matchmaking을 제공하지 않는다.

사용자가 Channel과 선택적 display name을 확정하면 scene은 endpoint와 expected Channel ID를 [`RemoteGameplaySession::Connect`](../src/PrivateServer.GameClient/Gameplay/Remote/RemoteGameplaySession.cs)에 전달한다.

새 connection은 이전 generation의 ready state, time sync, prediction, replica와 protocol staging을 재사용하지 않는다. Native Runtime이 transport connected event를 게시하고 Godot main thread가 이를 drain하면 Player mode는 `JoinWorldRequest`, Observer mode는 `ObserveWorldRequest`를 보낸다.

Server 쪽 NetworkRuntime은 accepted socket에 Runtime Session Key를 부여하고 `SessionAccepted` owning event를 World에 전달한다. World는 send capability를 가진 connected-only session을 등록하지만 이 단계에서는 Player나 Entity를 만들었다고 보지 않는다.

## Observer admission과 read-only path

Observer mode는 Connected-only session과 `ObserveWorldRequest` payload version을 검증한다. World는 현재 tick/cadence, arena bounds와 Channel ID를 가진 `ObserverReady`를 준비하고, gameplay가 활성화된 경우 현재 `RoundState` baseline을 먼저 기록한 뒤 Observer role을 commit한다. Baseline submit이 실패하면 session close를 요청하며 성공한 observer로 남기지 않는다.

Client는 expected Channel ID와 `ObserverReady`를 검증한 뒤 first time sync 없이 `Active`/`Observing`으로 전환한다. Observer Session은 movement/control packet을 보내거나 controlled prediction, detailed AOI replica를 만들지 않는다. World가 Player Session에도 보내는 `WorldOverview`의 완성된 chunk group을 받아 전체 player silhouette, leaderboard와 Active Area를 표현한다.

## Join prepare, baseline과 commit

[`WorldJoinIngress`](../src/PrivateServer.WorldServer/WorldJoinIngress.cpp)와 [`WorldIngressEventConsumer`](../src/PrivateServer.WorldServer/WorldIngressEventConsumer.cpp)는 `JoinWorldRequest`를 다음 경계로 처리한다.

1. Connected-only session, display name과 join precondition을 검증한다.
2. Player ID와 controlled World Entity를 준비한다.
3. Controlled entity spawn, authoritative score/round baseline과 `WorldReady` record를 outbound write slot에 준비한다.
4. `WorldReady`를 join baseline의 마지막 record로 둔다.
5. 필요한 outbound record가 모두 준비된 뒤 session-player-entity binding과 display name을 commit한다.

중간에 실패하면 준비한 entity와 gameplay state를 rollback하고 session close를 요청한다. 부분 baseline을 성공한 join으로 commit하지 않는다.

`WorldReady`는 다음 계약을 Client에 전달한다.

- authoritative Channel ID
- Player ID와 controlled Entity Key
- tick과 snapshot cadence 계약
- arena bounds와 movement preparation에 필요한 baseline

Client는 선택한 Channel ID와 authoritative Channel ID가 다르면 protocol fault로 처리하고 disconnect를 요청한다. `WorldReady` 전에 받은 controlled spawn과 gameplay baseline은 staging하며, `WorldReady`가 baseline 종료 경계가 된다.

Full AOI state는 join baseline과 별도 replication cycle에서 이어진다. Client는 baseline record 순서만으로 모든 remote entity를 받았다고 가정하지 않는다.

## First sync와 Playing 진입

`WorldReady`를 수락하면 Client는 controlled prediction과 remote replica store를 초기화하고 time-sync probe를 보낸다.

첫 time-sync response가 오기 전에도 authoritative state packet을 decode하고 local model에 반영할 수 있다. 그러나 movement/control send gate는 아직 닫혀 있다. Required sync가 확정되고 controlled identity가 준비돼야 session은 active가 되고 UI flow가 Playing으로 전환된다.

이 gate는 client clock projection과 server tick ordering이 준비되기 전에 prediction command를 authoritative input처럼 보내는 것을 막는다.

## Input에서 authoritative commit까지

Playing 중 Client는 protocol mode에 따라 movement input 또는 control-state command를 제출하고 controlled entity를 local prediction한다.

```text
Godot input
-> RemoteGameplaySession send
-> Managed / C ABI / native NrClient
-> TCP
-> Runtime Session Actor recv와 frame parse
-> NrToWorldEvent
-> World Ingress Pump와 sealed ingress
-> World ingress command admission
-> Coordinator-owned authoritative tick
```

World ingress는 session이 joined 상태인지, command가 현재 controlled Entity generation을 대상으로 하는지 확인한다.

- Movement input은 World-owned command store로 복사한다.
- Control-state command는 ordering을 확인한 뒤 authoritative control component에 반영한다.
- Stale generation이나 invalid ordering은 current entity state를 변경하지 않는다.

Coordinator는 같은 epoch의 ingress를 consume한 뒤 immutable tick input을 만들고 다음 단계들을 직렬화한다.

```text
lifecycle와 command state 확정
-> movement와 physics 계산
-> authoritative entity state commit
-> resource, boost, death, spawn과 active-area 계산
-> gameplay state commit
-> AOI와 replication plan 작성
```

각 계산 단계는 typed result를 만들고 Coordinator-owned commit 경계에서 canonical state에 반영된다. Client prediction 결과는 이 commit에 입력되는 authority가 아니며, 이후 controlled authoritative state를 받아 reconciliation한다.

## AOI와 outbound replication

Authoritative entity state가 commit되면 World는 spatial projection과 index를 갱신한다. Player Session의 AOI Viewpoint별로 enter/retain query를 수행하고 이전 visible set과 비교해 entered, stayed와 left set을 만든다.

- Entered entity는 spawn과 필요한 baseline state를 받는다.
- Stayed entity는 AOI-scoped state snapshot을 받는다.
- Left 또는 removed entity는 reason을 가진 remove packet을 받는다.
- Controlled entity의 authoritative state는 owner session에 별도 self-state로 전달한다.
- World overview는 Player HUD/minimap과 Observer의 Channel-wide presentation에 필요한 요약 경계를 제공한다.

World가 recipient와 semantic payload를 결정하고 outbound A/B slot을 seal한다. Publisher는 record 순서를 유지해 `NrGateway`에 제출하며, NetworkRuntime이 frame ownership과 per-session socket send를 담당한다.

Client는 native event payload를 Managed-owned memory로 바꾼 뒤 Godot main thread에서 drain한다. Controlled state는 prediction correction에, remote state는 replica history에, score/round/overview는 authoritative client model에 반영한 다음 scene, HUD와 effect를 갱신한다.

## Controlled death와 rebind

Controlled entity가 제거되면 Client는 old generation의 prediction과 control state를 폐기하고 SpawnPending 화면 흐름으로 돌아간다. 새 entity의 spawn과 rebind가 current controlled Entity Key를 확정해야 prediction을 다시 만들고 Playing으로 복귀한다.

World Entity ID만 같고 generation이 다른 state는 새 entity에 적용하지 않는다. AOI replica와 controlled prediction 모두 Entity ID와 generation을 함께 사용한다.

## Round lifecycle와 RoundResult

Waiting round는 필요한 joined participant가 준비되면 Running으로 전환된다. Running 중 score와 growth state는 authoritative gameplay commit으로 갱신된다.

현재 round 종료 조건은 configured deadline이다. Score target에 도달해도 deadline 전에 round를 끝내지 않는다. Ended commit이 발생하면 World는 연결돼 있고 alive인 player 중 가장 높은 growth 값을 가진 player를 winner로 계획하며, 같은 값이면 공동 winner를 유지한다.

Player recipient별 `RoundResult`와 recipient final growth가 없는 Observer용 result는 outbound record로 기록되고 Publisher를 거쳐 Client에 전달된다. Result packet은 server tick, round identity, recipient 결과와 winner identity를 포함하는 authoritative outcome이다. Client는 result 수신 시점의 최신 leaderboard snapshot을 보존해 winner Player ID를 display name으로 표현하고, 이름이 없으면 Player ID label로 fallback한다.

## Result, disconnect와 다음 연결

Client는 `RoundResult`를 local immutable result로 먼저 commit한 뒤 disconnect를 요청한다. Session이 Disconnecting이고 transport cleanup이 진행되는 동안에도 UI flow는 Result를 유지한다.

`TransportDisconnected` event를 consume하면 Client는 current generation의 ready configuration, prediction, replica, score/round state와 protocol assembler를 정리한다. Local RoundResult는 Result 화면에 필요하므로 이 cleanup보다 오래 유지한다.

World는 `SessionClosed`를 consume하면서 다음 state를 회수한다.

- Runtime send capability와 AOI visible set
- movement/control command state
- role에 따른 session-player-entity binding과 display name
- Player Session의 gameplay state와 owned entity; Observer Session은 이 state를 소유하지 않음

Ended round는 시간 경과만으로 자동 rematch하지 않는다. Joined player가 모두 cleanup되면 World가 Waiting으로 reset한다.

ChannelSelect 복귀도 자동이 아니다. 사용자가 Result 화면에서 return/reconnect를 선택해야 화면 흐름이 ChannelSelect로 이동한다. 다음 Connect 시도는 보존한 RoundResult와 transient presentation identity를 지우고 fresh baseline을 요구한다.

## Failure와 recovery

| 상황 | 현재 동작 |
| --- | --- |
| connect 또는 transport setup 실패 | Client fault를 보존하고 join request를 만들지 않음 |
| join validation 또는 outbound prepare 실패 | World가 준비 state를 rollback하고 session close 요청 |
| observer payload/baseline/admission 실패 | Observer role을 commit하지 않고 reject 또는 session close 요청 |
| expected Channel과 WorldReady Channel 불일치 | Client protocol fault와 disconnect 요청 |
| first time sync 전 input | Client movement/control gate에서 제출하지 않음 |
| stale entity generation command/state | Current entity state를 변경하지 않고 reject 또는 ignore |
| World phase 또는 outbound write 실패 | partial success를 계속 publish하지 않고 controlled stop |
| unexpected transport close | 양쪽 owner가 session/generation-local state를 정리하고 Client가 fault detail 보존 |
| controlled entity death | Client prediction reset과 SpawnPending, 새 rebind 뒤 Playing 복귀 |
| RoundResult 뒤 disconnect | Result는 보존하고 transport와 World session state는 정리 |

## 관련 구현과 테스트

| 독자 질문 | 관련 구현 | 관련 테스트 |
| --- | --- | --- |
| Channel 선택과 화면 흐름은 어디서 시작되는가? | [`GameplayChannelDirectory.cs`](../src/PrivateServer.GameClient/Gameplay/Flow/GameplayChannelDirectory.cs), [`RemoteGameplayScene.cs`](../src/PrivateServer.GameClient/Gameplay/Presentation/RemoteGameplayScene.cs) | [`GameplayChannelDirectoryTests.cs`](../src/PrivateServer.GameClient.Tests/GameplayChannelDirectoryTests.cs), [`GameplayFlowTests.cs`](../src/PrivateServer.GameClient.Tests/GameplayFlowTests.cs) |
| Observer admission과 read-only overview는 어디서 갈라지는가? | [`WorldIngressEventConsumer.cpp`](../src/PrivateServer.WorldServer/WorldIngressEventConsumer.cpp), [`RemoteGameplaySession.cs`](../src/PrivateServer.GameClient/Gameplay/Remote/RemoteGameplaySession.cs), [`WorldOverviewPresentation.cs`](../src/PrivateServer.GameClient/Gameplay/Presentation/WorldOverviewPresentation.cs) | [`WorldIngressEventConsumerTests.cpp`](../src/PrivateServer.WorldServer.Tests/WorldIngressEventConsumerTests.cpp), [`RemoteGameplaySessionTests.cs`](../src/PrivateServer.GameClient.Tests/RemoteGameplaySessionTests.cs), [`GameplayFlowTests.cs`](../src/PrivateServer.GameClient.Tests/GameplayFlowTests.cs) |
| Join baseline과 binding은 어떤 경계에서 commit되는가? | [`WorldJoinIngress.cpp`](../src/PrivateServer.WorldServer/WorldJoinIngress.cpp), [`WorldIngressEventConsumer.cpp`](../src/PrivateServer.WorldServer/WorldIngressEventConsumer.cpp) | [`WorldIngressEventConsumerTests.cpp`](../src/PrivateServer.WorldServer.Tests/WorldIngressEventConsumerTests.cpp), [`WorldGameplayReplicationTests.cpp`](../src/PrivateServer.WorldServer.Tests/WorldGameplayReplicationTests.cpp) |
| Public Runtime을 거친 실제 join/input/state path는 무엇인가? | NetworkRuntime public API와 World ingress/outbound adapter | [`WorldPublicLoopbackTests.cpp`](../src/PrivateServer.WorldServer.Tests/WorldPublicLoopbackTests.cpp) |
| Input은 어디서 authoritative tick state가 되는가? | [`WorldMovementCommandAdmission.cpp`](../src/PrivateServer.WorldServer/WorldMovementCommandAdmission.cpp), [`WorldControlCommandAdmission.cpp`](../src/PrivateServer.WorldServer/WorldControlCommandAdmission.cpp), [`WorldDoubleBufferedTickCoordinator.h`](../src/PrivateServer.WorldServer/WorldDoubleBufferedTickCoordinator.h) | World ingress, movement와 physics tests |
| AOI recipient와 replication packet은 누가 계획하는가? | [`WorldSpatialIndex.cpp`](../src/PrivateServer.WorldServer/WorldSpatialIndex.cpp), [`WorldAoiPlanner.cpp`](../src/PrivateServer.WorldServer/WorldAoiPlanner.cpp), [`WorldReplicationPlan.cpp`](../src/PrivateServer.WorldServer/WorldReplicationPlan.cpp) | [`WorldAoiPlannerTests.cpp`](../src/PrivateServer.WorldServer.Tests/WorldAoiPlannerTests.cpp), World replication tests |
| Round는 언제 끝나고 winner는 어떻게 선택되는가? | [`WorldGameplayPhase.cpp`](../src/PrivateServer.WorldServer/WorldGameplayPhase.cpp), [`WorldRoundResultPlanner.cpp`](../src/PrivateServer.WorldServer/WorldRoundResultPlanner.cpp) | [`WorldGameplayPhaseTests.cpp`](../src/PrivateServer.WorldServer.Tests/WorldGameplayPhaseTests.cpp), [`WorldGameplayReplicationTests.cpp`](../src/PrivateServer.WorldServer.Tests/WorldGameplayReplicationTests.cpp) |
| Client result와 reconnect state는 어디서 분리되는가? | [`RemoteGameplaySession.cs`](../src/PrivateServer.GameClient/Gameplay/Remote/RemoteGameplaySession.cs), [`GameplayFlow.cs`](../src/PrivateServer.GameClient/Gameplay/Flow/GameplayFlow.cs) | [`RemoteGameplaySessionTests.cs`](../src/PrivateServer.GameClient.Tests/RemoteGameplaySessionTests.cs), [`GameplayFlowTests.cs`](../src/PrivateServer.GameClient.Tests/GameplayFlowTests.cs) |

## 상세 문서

- [전체 시스템 아키텍처](system-architecture.md)
- [Session Actor ownership과 I/O lifetime](network-runtime/session-actor-ownership-and-io-lifetime.md)
- [World Host 설정과 Process Lifecycle](world-server/host-configuration-and-process-lifecycle.md)
- [World Server 실행 ownership과 fixed-step pipeline](world-server/runtime-ownership-and-tick-pipeline.md)
- [Gameplay Protocol Reference](world-server/gameplay-protocol-reference.md)
- [Authoritative Gameplay와 Round 계약](world-server/authoritative-gameplay-and-round-contract.md)
- [AOI, Active Area와 Replication 계약](world-server/aoi-active-area-and-replication.md)
- [Game Client main-thread session과 presentation lifecycle](game-client/main-thread-session-and-presentation-lifecycle.md)

## 지원 범위와 제약

- Channel directory는 local static configuration이다. Dynamic discovery, health routing과 matchmaking은 현재 범위가 아니다.
- Client는 server-authoritative result를 표현하는 thin client다. Offline authority나 client-side gameplay fallback은 제공하지 않는다.
- Ended round는 연결된 participant가 정리된 뒤 Waiting으로 reset되며 automatic rematch countdown을 제공하지 않는다.
- 이 문서는 특정 workload capacity, latency나 throughput을 보장하지 않는다.
