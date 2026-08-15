# Main thread session과 presentation lifecycle

> Document status: Reviewed
> Baseline: c0bd3a8e5f1861c6dc1321381b6c58ca7a374030
> Last reviewed: 2026-08-16

## 핵심 답

Game Client는 native NetworkRuntime event를 Godot main thread에서 bounded하게 drain한 뒤 `RemoteGameplaySession`의 generation-local model로 옮기고, 그 model의 현재 snapshot을 scene node와 HUD에 반영한다. Session mode는 controlled prediction과 detailed replica를 사용하는 Player, 또는 `WorldOverview`만 표현하는 read-only Observer 중 하나다.

`RemoteGameplayScene`가 production gameplay 진입점이자 Godot object의 lifetime owner다. `RemoteGameplaySession`은 transport, protocol ordering, prediction, replica와 authoritative gameplay state를 소유하지만 Godot node는 소유하지 않는다. 이 분리 때문에 비동기 transport state와 scene tree mutation이 같은 worker에서 경쟁하지 않는다.

```text
native NrClient event
-> C ABI event handle
-> Managed owning event와 payload
-> gameplay transport event
-> RemoteGameplaySession model mutation
-> prediction / replica presentation snapshot
-> RemoteGameplayScene node, effect, HUD 갱신
```

Disconnect 요청만으로 generation-local state를 즉시 지우지 않는다. Main thread가 `TransportDisconnected` event를 소비할 때 transport generation에 속한 ready state, prediction, replica, score와 protocol assembler를 정리한다. Reconnect는 같은 transport wrapper를 다시 사용할 수 있어도 새로운 gameplay generation이다.

## 책임과 제외 범위

이 문서는 다음 질문을 다룬다.

- Native event payload는 어느 지점에서 Managed-owned memory가 되는가?
- Transport event와 Godot presentation은 어느 thread에서 순서화되는가?
- Session model, prediction, replica와 scene node의 owner는 누구인가?
- Disconnect, reconnect, controlled entity 교체와 scene teardown에서 무엇을 정리하는가?

다음 내용은 별도 문서의 책임이다.

- Native client 내부의 IOCP와 socket lifetime
- World Server의 authoritative simulation과 AOI recipient 선택
- Gameplay packet field별 wire encoding
- capacity와 성능 결과

## Lifetime owner와 mutation owner

| 대상 | Lifetime owner | 변경 경계 |
| --- | --- | --- |
| flow, session과 Godot presentation object | `RemoteGameplayScene` | Godot main thread의 UI callback과 `_Process` |
| transport generation과 protocol/session model | `RemoteGameplaySession` | `DrainFrame`과 active-frame operation |
| public Managed native client | `NetworkRuntimeGameplayTransport` | session이 호출하는 transport operation |
| native client opaque handle | `NetworkRuntimeClient`의 `SafeClientHandle` | Managed adapter lifecycle |
| native event opaque handle | `TryPopEvent` scope의 `SafeClientEventHandle` | event accessor 뒤 즉시 release |
| packet payload | `NetworkRuntimeEvent`의 managed array | native payload를 복사한 뒤 event가 소유 |
| remote replica와 snapshot history | `RemoteReplicaStore` | session packet application과 presentation sampling |
| controlled prediction | `RemoteGameplaySession` | input advance와 authoritative correction |
| controlled/remote Godot node | `RemoteGameplayScene` | current presentation snapshot에 따라 create, update, `QueueFree` |
| transient visual/audio cue identity | `GameplayPresentationCueProjector`와 scene | transport generation과 controlled identity 전환에 맞춰 reset |

기본 `RemoteGameplaySession`은 `IRemoteGameplayTransport`를 소유하며, production transport 구현이 `NetworkRuntimeClient`를 소유한다. Session이 native client concrete type이나 C ABI handle을 직접 다루지는 않는다.

## Native event에서 Managed model까지

[`NetworkRuntimeClient::TryPopEvent`](../../src/PrivateServer.NetworkRuntime.Managed/NetworkRuntimeClient.cs)는 native queue에서 opaque event handle을 얻고 public accessor로 kind와 payload를 읽는다.

Packet event의 payload는 다음 순서로 수명이 바뀐다.

1. Native event가 payload view를 소유한다.
2. Managed adapter가 view의 bytes를 새 managed array로 복사한다.
3. Adapter가 native event handle을 release한다.
4. `NetworkRuntimeEvent`가 managed payload를 소유한다.
5. `NetworkRuntimeGameplayTransport`가 같은 owning memory를 gameplay transport event로 전달한다.
6. `RemoteGameplaySession`이 event를 decode하고 domain state로 반영한다.

따라서 session model과 Godot presentation은 native event handle이나 native payload view를 오래 보관하지 않는다. C ABI event가 파괴된 뒤에도 Managed event payload는 독립적으로 유효하다.

Connect, disconnect, shutdown과 send는 [`IRemoteGameplayTransport`](../../src/PrivateServer.GameClient/Gameplay/Remote/RemoteGameplayTransport.cs)가 제공하는 작은 seam을 통한다. Test transport는 같은 seam으로 session state machine을 검증할 수 있고, production transport만 public Managed Runtime에 의존한다.

## Main-thread frame 순서

[`RemoteGameplayScene::_Process`](../../src/PrivateServer.GameClient/Gameplay/Presentation/RemoteGameplayScene.cs)는 한 frame에서 다음 순서를 사용한다.

```text
transport event bounded drain
-> packet decode와 session model 변경
-> session observation을 flow state에 반영
-> Player가 Playing이면 input sample과 controlled prediction/send 진행
-> remove effect에 필요한 이전 node 위치 보존
-> controlled/remote presentation snapshot 적용
-> current snapshot에 없는 node 제거
-> state와 removal effect/audio cue 적용
-> HUD와 developer overlay 갱신
-> flow screen 표시 갱신
```

이 순서에서 native worker는 scene tree를 변경하지 않는다. `RemoteGameplaySession`도 Godot node를 만들거나 `QueueFree`하지 않는다. Scene이 drain 결과와 현재 model snapshot을 소비해 node를 갱신하므로 transport event, prediction과 presentation의 관찰 순서가 main thread 안에서 고정된다.

현재 구현은 별도 pending presentation command queue를 두지 않는다. Session snapshot을 매 frame 직접 scene에 투영한다.

## Flow state와 session state

화면 흐름과 transport/protocol 수명은 서로 다른 state machine이다.

### 사용자 화면 흐름

```text
ChannelSelect
|-- PlayerSetup -> Connecting -> Joining -> SpawnPending -> Playing
|   |-- controlled entity 제거: SpawnPending
|   `-- matching spawn/rebind: Playing
`-- Observer connect -> Joining -> Observing

Playing 또는 Observing
-> RoundResult: Result

transport/protocol fault
-> Error

Result 또는 Error
-> 명시적 사용자 선택
-> ChannelSelect 또는 Exiting
```

### Session state

```text
Idle
-> Connecting
-> AwaitingBaseline
|-- Player: AwaitingFirstTimeSync -> Active
`-- Observer: Active
-> Disconnecting
-> TransportDisconnected event 소비
-> Idle

ordering, decode, prediction 또는 transport failure
-> Faulted
-> transport disconnect event 소비
-> Idle with fault detail
```

`GameplayFlow`는 session observation을 화면 상태로 투영한다. Player Session이 `Active`여도 controlled entity가 아직 없거나 재생성 대기 중이면 화면 흐름은 `SpawnPending`을 유지한다. Observer Session이 `Active`가 되면 controlled entity 조건 없이 `Observing`으로 전환한다. Round result가 local state에 commit되면 flow는 session의 disconnect 진행과 별개로 `Result`를 유지한다.

## Baseline과 active generation

Connect가 시작되면 session은 이전 round result와 generation-local state를 정리하고 transport connect를 요청한다. 성공한 connect 요청마다 local transport generation을 전진시킨다.

Transport connected event를 받으면 Player mode는 join request, Observer mode는 observe request를 보내고 baseline을 기다린다. Player는 World ready와 baseline entity state를 받은 뒤에도 첫 time sync가 확정되기 전에는 movement를 열지 않는다. Observer는 `RoundState`와 `ObserverReady`를 순서대로 수락하고 expected Channel ID를 확인한 뒤 별도 time sync 없이 Active가 된다.

Player Active state는 authoritative packet ordering과 entity generation을 확인한 뒤 prediction, detailed replica와 gameplay model을 갱신한다. Observer Active state는 complete `WorldOverview` group, `RoundState`와 `RoundResult`만 수락하며 input scheduler나 controlled prediction을 만들지 않는다.

`RemoteReplicaStore`는 Entity ID와 generation을 함께 key로 사용한다. 같은 Entity ID라도 generation이 바뀌면 이전 replica state를 새 entity에 적용하지 않는다. Spawn은 replica metadata와 history를 시작하고, state packet은 해당 generation history를 전진시키며, remove는 matching replica만 정리한다.

Controlled entity가 제거되면 session은 prediction과 control state를 폐기하고 spawn pending으로 전환한다. 이후 matching spawn과 rebind packet이 새 controlled identity를 확정해야 replica가 controlled prediction으로 승격된다.

## Prediction과 presentation 경계

Client prediction은 입력 반응을 표현하지만 authoritative ownership을 바꾸지 않는다.

- Session은 controlled entity의 logical prediction state를 전진시킨다.
- Authoritative controlled state는 prediction의 logical state를 교정한다.
- Presentation snapshot은 correction을 render position에 투영한다.
- Remote entity는 server snapshot history에서 presentation state를 sample한다.
- Score, round, active area와 entity lifetime은 authoritative packet으로만 변경한다.

[`RemoteGameplayScene`](../../src/PrivateServer.GameClient/Gameplay/Presentation/RemoteGameplayScene.cs)은 controlled snapshot으로 local node와 camera를 갱신하고, remote presentation snapshot 집합으로 remote node를 create/update한다. 현재 snapshot에 없는 remote key는 scene dictionary에서 제거하고 node에 `QueueFree`를 요청한다.

Observer mode에서는 [`WorldOverviewPresentation`](../../src/PrivateServer.GameClient/Gameplay/Presentation/WorldOverviewPresentation.cs)이 overview player silhouette와 leaderboard를 그린다. Scene은 Player용 controlled/remote replica node를 비워 두며 gameplay input을 sample하지 않는다. `--observe-channel <id>` launch option은 Channel directory의 동일 ID endpoint를 찾아 이 flow를 자동 시작한다.

Removal effect는 node를 제거하기 전에 위치를 capture한다. 그 뒤 presentation snapshot을 적용하고 stale node를 제거한 다음 one-shot cue를 실행한다. 이 순서 때문에 model에서 이미 제거된 entity도 마지막 화면 위치에서 effect를 표현할 수 있다.

## Disconnect와 reconnect

`RemoteGameplaySession::Disconnect`가 성공하면 state는 `Disconnecting`이 된다. 이 시점에는 generation-local model을 바로 지우지 않는다. Runtime이 실제 transport 종료를 event로 전달할 때까지 화면과 result가 마지막 상태를 관찰할 수 있다.

`TransportDisconnected` event를 consume하면 session은 다음 generation-local state를 정리한다.

- baseline과 player identity staging
- Player/Observer ready configuration, session mode, Channel ID와 display name
- time sync와 movement/control scheduler
- controlled prediction과 replica store
- authoritative score, round와 world overview
- protocol group assembler와 static obstacle cache
- current-generation anomaly와 frame time state

예상하지 못한 disconnect이고 기존 fault나 round result가 없다면 transport fault를 기록한다. Round result는 disconnect 전에 별도 local result로 commit되므로 generation-local state를 지운 뒤에도 Result 화면을 위해 남는다. 다음 Connect 시도는 이전 result를 먼저 제거한다.

Reconnect는 transport object를 재사용할 수 있지만 새로운 generation을 사용하고 fresh baseline을 요구한다. Scene의 cue projector와 transient effect도 transport generation 또는 controlled identity가 바뀌면 이전 generation의 boost, spawn과 effect identity를 reset한다.

## Scene teardown

Godot tree에서 gameplay scene이 나가면 `_ExitTree`가 session을 dispose한다. 명시적 application exit도 process를 멈추고 session을 dispose한 뒤 tree quit를 요청한다.

Session dispose는 transport에 shutdown을 요청한 뒤 transport와 native client handle을 해제한다. 이 호출은 teardown ownership을 회수하는 경계이며, 모든 remote peer가 graceful disconnect를 관찰했다고 보장하는 protocol은 아니다.

Godot node의 생성과 `QueueFree`는 scene code가 직접 소유한다. Pure session/flow tests는 Godot node 자체를 만들지 않고 transport ordering, generation reset과 presentation input contract를 확인한다.

## Interface와 invariant

- Godot scene tree mutation은 `RemoteGameplayScene`의 main-thread path에서만 수행한다.
- `RemoteGameplaySession`은 Godot node를 소유하거나 직접 변경하지 않는다.
- Native packet payload는 native event handle release 전에 Managed-owned memory로 복사한다.
- Flow state와 session state는 분리하며, session state 하나를 화면 하나와 동일시하지 않는다.
- Movement는 active session, controlled identity와 required sync가 모두 준비된 뒤에만 제출한다.
- Entity lifetime은 Entity ID와 generation을 함께 사용한다.
- Disconnect 요청과 disconnect 완료 event는 서로 다른 시점이다.
- Generation-local state는 `TransportDisconnected` event를 consume할 때 정리한다.
- Round result와 result 수신 시점의 최신 leaderboard snapshot은 Result 화면을 위해 disconnect cleanup보다 오래 살 수 있지만 다음 connect 전에 제거한다.
- Reconnect는 이전 baseline, prediction, replica와 transient cue identity를 재사용하지 않는다.
- Observer는 controlled identity, first time sync, input send와 detailed replica를 사용하지 않는다.

## Failure와 recovery

| 상황 | 현재 동작 |
| --- | --- |
| connect operation 실패 | fault detail을 기록하고 새 gameplay generation을 active로 만들지 않음 |
| WorldReady의 Channel 불일치 | protocol fault와 disconnect 경로로 전환 |
| ObserverReady의 Channel 불일치 또는 observer-only packet ordering 위반 | protocol fault와 disconnect 경로로 전환 |
| packet decode 또는 ordering 실패 | session을 faulted로 표시하고 transport disconnect 요청 |
| receive pressure disconnect | Runtime이 전달한 transport 상태를 fault detail로 보존 |
| controlled entity 제거 | prediction과 control state를 정리하고 rebind 전까지 spawn pending 유지 |
| unexpected transport disconnect | generation-local state를 정리하고 Error 화면에 필요한 fault 보존 |
| round result 뒤 disconnect | result를 보존하고 명시적 사용자 선택 전까지 Result 화면 유지 |
| scene teardown | transport shutdown을 요청하고 owned handle을 dispose |

Error와 Result에서 자동 reconnect하지 않는다. 사용자가 ChannelSelect로 명시적으로 돌아간 뒤 새 connection과 baseline을 시작한다.

## 관련 구현과 테스트

| 독자 질문 | 관련 구현 | 관련 테스트 |
| --- | --- | --- |
| 화면 흐름과 session state는 어떻게 분리되는가? | [`GameplayFlow.cs`](../../src/PrivateServer.GameClient/Gameplay/Flow/GameplayFlow.cs), [`RemoteGameplaySessionState.cs`](../../src/PrivateServer.GameClient/Gameplay/Remote/RemoteGameplaySessionState.cs) | [`GameplayFlowTests.cs`](../../src/PrivateServer.GameClient.Tests/GameplayFlowTests.cs) |
| Observer launch, admission과 overview presentation은 어디서 소유하는가? | [`GameplayLaunchOptions.cs`](../../src/PrivateServer.GameClient/Gameplay/Flow/GameplayLaunchOptions.cs), [`RemoteGameplaySession.cs`](../../src/PrivateServer.GameClient/Gameplay/Remote/RemoteGameplaySession.cs), [`WorldOverviewPresentation.cs`](../../src/PrivateServer.GameClient/Gameplay/Presentation/WorldOverviewPresentation.cs) | [`GameplayLaunchOptionsTests.cs`](../../src/PrivateServer.GameClient.Tests/GameplayLaunchOptionsTests.cs), [`RemoteGameplaySessionTests.cs`](../../src/PrivateServer.GameClient.Tests/RemoteGameplaySessionTests.cs), [`GameplayFlowTests.cs`](../../src/PrivateServer.GameClient.Tests/GameplayFlowTests.cs) |
| Native payload는 언제 Managed-owned memory가 되는가? | [`NetworkRuntimeClient.cs`](../../src/PrivateServer.NetworkRuntime.Managed/NetworkRuntimeClient.cs), [`SafeHandles.cs`](../../src/PrivateServer.NetworkRuntime.Managed/SafeHandles.cs) | [`Managed Smoke`](../../src/PrivateServer.NetworkRuntime.Managed.Smoke/Program.cs) |
| Disconnect와 reconnect는 어떤 state를 정리하는가? | [`RemoteGameplaySession.cs`](../../src/PrivateServer.GameClient/Gameplay/Remote/RemoteGameplaySession.cs) | [`RemoteGameplaySessionTests.cs`](../../src/PrivateServer.GameClient.Tests/RemoteGameplaySessionTests.cs) |
| Replica generation과 snapshot history는 어디서 소유하는가? | [`RemoteReplicaStore.cs`](../../src/PrivateServer.GameClient/Gameplay/Replication/RemoteReplicaStore.cs) | [`RemoteReplicaStoreTests.cs`](../../src/PrivateServer.GameClient.Tests/RemoteReplicaStoreTests.cs) |
| Prediction과 authoritative correction 경계는 무엇인가? | [`ControlledEntityPrediction.cs`](../../src/PrivateServer.GameClient/Gameplay/Prediction/ControlledEntityPrediction.cs) | [`ControlledEntityPredictionTests.cs`](../../src/PrivateServer.GameClient.Tests/ControlledEntityPredictionTests.cs) |
| Session snapshot은 Godot node에 어떤 순서로 반영되는가? | [`RemoteGameplayScene.cs`](../../src/PrivateServer.GameClient/Gameplay/Presentation/RemoteGameplayScene.cs), [`GameplayPresentationCueProjector.cs`](../../src/PrivateServer.GameClient/Gameplay/Presentation/GameplayPresentationCueProjector.cs) | [`GameplayPresentationCueProjectorTests.cs`](../../src/PrivateServer.GameClient.Tests/GameplayPresentationCueProjectorTests.cs), [`GodotWorldTransformTests.cs`](../../src/PrivateServer.GameClient.Tests/GodotWorldTransformTests.cs) |
| Authoritative score와 round는 generation cleanup과 어떻게 분리되는가? | [`AuthoritativeGameplayState.cs`](../../src/PrivateServer.GameClient/Gameplay/Model/AuthoritativeGameplayState.cs) | [`AuthoritativeGameplayStateTests.cs`](../../src/PrivateServer.GameClient.Tests/AuthoritativeGameplayStateTests.cs) |

## 관련 문서

- [NetworkRuntime Public DLL 경계](../network-runtime/public-runtime-boundary.md)
- [Session Actor ownership과 I/O lifetime](../network-runtime/session-actor-ownership-and-io-lifetime.md)
- [World Server 실행 ownership과 fixed-step pipeline](../world-server/runtime-ownership-and-tick-pipeline.md)

## 지원 범위와 제약

- Game Client는 thin presentation client다. Server-authoritative collision, score, round, spawn과 AOI decision을 소유하지 않는다.
- Observer presentation은 시각 검증용이며 해당 실행에 추가 session과 rendering load를 만든다.
- Channel directory는 local configuration이며 runtime population discovery나 matchmaking을 제공하지 않는다.
- Scene node creation, `QueueFree`와 `_ExitTree`는 production Godot code 경로다. Pure session/flow tests는 이 engine lifecycle 자체를 실행하지 않는다.
- Session dispose는 owned transport와 handle을 회수하지만 application-level graceful disconnect 완료를 보장하지 않는다.
- 이 문서는 특정 frame budget이나 presentation 성능을 보장하지 않는다.
