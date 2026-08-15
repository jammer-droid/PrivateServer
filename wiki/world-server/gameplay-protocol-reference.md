# Gameplay Protocol Reference

> Document status: Reviewed
> Baseline: 1508dacf340e52cb4ec67e7e7a60d05755510553
> Last reviewed: 2026-08-12

## 핵심 답

Private Server의 wire message는 NetworkRuntime transport frame과 World-owned gameplay semantic payload로 나뉜다. NetworkRuntime은 packet type, framing과 payload lifetime만 소유하고, World Server가 gameplay payload의 version, field, enum과 numeric 의미를 검증한다.

```text
Client gameplay value
-> C# semantic payload encode
-> NrClient가 transport frame 생성
-> TCP / IOCP
-> Runtime frame parse와 WorldIngress routing
-> World C++ semantic payload decode
-> session/entity admission과 authoritative command

World authoritative result
-> C++ semantic payload encode
-> NrGateway가 transport frame 생성
-> TCP
-> NrClient event
-> C# version dispatch와 semantic payload decode
-> session ordering과 generation 검증
```

Packet type은 message의 역할을, semantic payload 첫 field인 `payloadVersion`은 그 역할 안의 wire layout을 구분한다. 기존 version의 field 의미를 바꾸지 않고 호환되지 않는 변경은 새 payload version 또는 새 packet type으로 표현한다.

## 책임과 제외 범위

이 문서는 다음을 설명한다.

- transport frame과 gameplay payload의 책임 분리
- C2S/S2C packet catalog와 현재 runtime path의 payload version
- packet별 핵심 field와 identity 의미
- join, active gameplay, respawn과 result의 ordering contract
- decode, admission과 client application 실패 의미
- C++ source와 C# mirror를 함께 변경해야 하는 위치

Packet의 byte offset과 최대 collection 크기는 각 C++ packet header의 `Wire` 선언이 최종 기준이다. Gameplay 계산식, AOI query와 round rule의 내부 구현은 별도 World 계약이 담당한다.

## Wire layer와 공통 invariant

```text
Runtime transport frame
|-- packet length
|-- packet type
|-- Runtime protocol version
|-- flags
`-- semantic payload
    |-- payloadVersion
    `-- packet-specific fields
```

- Transport header는 [`NrPacketHeader`](../../src/PrivateServer.NetworkRuntime.Internal/NrPacketHeader.h)와 Runtime parser가 소유한다. World codec은 transport header를 다시 읽거나 쓰지 않는다.
- Gameplay scalar는 fixed-width integer와 `float32`를 little-endian으로 encode한다.
- `float32` decode는 negative zero를 canonical zero로 정규화한다.
- Compiler struct layout, padding이나 `reinterpret_cast`에 wire format을 맡기지 않는다.
- 각 packet type은 자신의 `Wire` layout, `Encode`, `Decode`와 value validation을 함께 소유한다.
- Decode는 length와 version을 확인한 뒤 enum과 numeric invariant를 검증하고, 성공할 때만 output value를 commit한다.
- Runtime의 received payload view는 `NrToWorldEvent` lifetime까지만 유효하다. World는 그 안에서 typed command 또는 World-owned value로 변환한다.
- C# decoder도 packet type과 payload version을 함께 분기하며, native payload는 Managed-owned bytes로 복사된 뒤 decode된다.

공통 protocol error는 `InvalidArgument`, `InvalidLength`, `UnsupportedVersion`, `InvalidEnum`, `InvalidNumeric`을 구분한다. 이 값은 semantic codec 결과이며 NetworkRuntime의 `NrStatus`와 다른 World-owned error다.

## C2S packet catalog

| `C2SPacketType` | Runtime path version | 핵심 field | World admission 의미 |
| --- | --- | --- | --- |
| `JoinWorldRequest` | V2 | 선택적 `displayName` | connected-only Runtime Session에 Player와 controlled entity를 준비하고 baseline 성공 뒤 binding commit |
| `MovementInput` | V1 | controlled generation, target server tick, encoded movement axes | joined session의 현재 controlled generation과 tick window를 통과한 최신 movement command 저장 |
| `WorldTimeSyncRequest` | V1 | probe sequence | joined session에 마지막 완료 server tick을 같은 sequence로 응답 |
| `ControlStateCommand` | V2 | controlled generation, input sequence, turn state, boost state | 현재 generation과 ordering을 통과한 control component 갱신 |

[`WorldPacketTypes.h`](../../src/PrivateServer.WorldServer/WorldPacketTypes.h)의 `C2SWorldIngressPacketTypes`가 gameplay packet을 Runtime `WorldIngress` lane에 등록하는 catalog다. 이 등록은 packet을 World로 전달할지 결정할 뿐 payload version이나 gameplay precondition을 검증하지 않는다.

### JoinWorldRequest V2

`displayName`은 account identity나 credential이 아니라 현재 Channel 참가 session의 표시값이다. 빈 값은 허용하며, 값이 있으면 [`MaximumPlayerDisplayNameBytes`](../../src/PrivateServer.WorldServer/WorldProtocolWireCodec.h) 범위 안의 영문 대소문자와 숫자만 허용한다. Join은 accepted 상태이지만 아직 joined되지 않은 session에서만 처리하며, 종료된 round에 새 participant를 commit하지 않는다.

V1 codec은 빈 join payload의 이전 layout을 보존하지만 현재 World join ingress와 Game Client는 V2를 사용한다. V2를 V1 의미로 추측해서 decode하지 않는다.

### MovementInput V1

Client는 Runtime Session Key, Player ID나 Entity ID를 payload로 보내지 않는다. World는 Runtime Session binding으로 actor를 찾고 `controlledEntityGeneration`으로 respawn 이전 input을 거른다.

`targetServerTick`은 target-tick ingress에서 scheduling key로 사용된다. 늦은 input과 stale generation은 current state를 변경하지 않는다. 허용 범위보다 앞선 target이나 같은 target tick의 중복은 protocol policy violation으로 집계되며 반복 위반은 session close로 이어질 수 있다.

### ControlStateCommand V2

`inputSequence`는 control state 변경의 단조 증가 ordering key다. `TurnState`는 `Straight`, `Left`, `Right`, `BoostState`는 `Off`, `On`만 유효하다. Client는 Active state에서 V2 controlled snapshot을 관측하고 controlled entity가 살아 있을 때 held state가 바뀌면 새 command를 보낸다. World는 현재 controlled generation과 마지막 적용 sequence보다 새로운 command만 반영한다.

## S2C packet catalog

| `S2CPacketType` | Codec layout | 현재 runtime path | 핵심 field와 의미 |
| --- | --- | --- | --- |
| `WorldReady` | V1, V2 | V2 | Player ID, controlled Entity Key, server tick/cadence, command slack, map bounds; V2는 Channel ID와 display name 추가 |
| `EntitySpawn` | V1, V2 | V2 | server tick, Entity Key, kind/archetype, primary shape와 initial kinematic state; V2는 Player identity와 display name 추가 |
| `ControlledEntityState` | V1, V2 | gameplay V2, non-gameplay fallback V1 | self authoritative state; V2는 control acknowledgement, head/body, diameter, growth와 boost state 제공 |
| `EntityStateBatch` | V1, V2 | gameplay V2, compact fallback V1 | V1은 remote kinematic record batch, V2는 snapshot group으로 나뉠 수 있는 whole-body record batch |
| `EntityRemove` | V1 | V1 | server tick, Entity Key와 remove reason |
| `ScoreState` | V1 | V1 | Player ID의 full authoritative score |
| `RoundState` | V1 | V1 | round identity, phase, phase deadline, score contract와 single-winner compatibility field |
| `WorldTimeSyncResponse` | V1 | V1 | request probe sequence와 마지막 완료 server tick |
| `ControlledEntityRebind` | V1 | V1 | Player ID, 이전 Entity Key와 새 controlled Entity Key |
| `WorldOverviewSnapshot` | V2, V3 | V3 | map/Active Area, alive player silhouette와 leaderboard의 chunk group; V3는 leaderboard display name 추가 |
| `RoundResult` | V2 | V2 | end tick, round identity, winning growth, recipient final growth와 ordered winner list |

현재 World join은 `WorldReady` V2와 `EntitySpawn` V2를 생성하고, overview publisher와 active Client session은 V3를 사용한다. Client codec에는 Overview V2 decoder가 남아 있지만 active session이 소비하는 end-to-end contract는 V3다. `ControlledEntityState`와 `EntityStateBatch`는 compact non-gameplay path와 whole-body gameplay path를 각 version으로 구분한다.

### Entity identity와 spawn/remove

Wire entity identity는 `entityId + generation`인 World Entity Key다. 같은 Entity ID라도 generation이 다르면 다른 lifetime이다.

- `EntitySpawn`은 해당 key의 replica baseline을 만든다.
- V2 Player spawn만 Player ID와 display name을 가진다. Resource와 StaticObstacle은 Player identity를 갖지 않는다.
- `EntityRemove.LeftAoi`는 client replica lifetime만 끝내고 authoritative World entity를 파괴하지 않는다.
- `Destroyed`, `Collected`, `SessionClosed`, `RoundReset`은 서로 다른 remove 이유를 표현한다. `Collected` 자체가 score 값은 아니며 score authority는 `ScoreState` 또는 gameplay result에 있다.
- 수신자의 현재 controlled entity는 일반 remote replica 집합과 분리한다.

### Self와 remote state

`ControlledEntityState`는 recipient의 binding이 controlled entity를 암시하므로 Entity ID를 반복하지 않고 generation을 포함한다. V2의 `lastProcessedControlSequence`는 그 sequence까지 control command가 authoritative state에 반영됐음을 뜻한다.

`EntityStateBatch` V2는 같은 `snapshotId`, server tick과 chunk metadata를 가진 group으로 whole-body record를 전달한다. 하나의 entity record를 packet 사이에서 나누지 않으며 Client는 완성된 group만 replica store에 commit한다. V1 batch는 full kinematic record이며 codec은 AOI membership이나 record ordering을 결정하지 않는다.

### Overview와 final result

`WorldOverviewSnapshot`은 detailed AOI replica와 다른 전역 요약 경계다. 같은 `overviewId`의 chunk가 map bounds, Active Area와 group metadata에 합의해야 하며 Client는 group 전체가 완성된 뒤 overview를 교체한다. V3 leaderboard entry는 display name을 함께 전달한다.

`RoundState` V1의 winner field는 single-winner compatibility state다. 공동 winner와 winner 없음까지 포함한 최종 outcome은 `RoundResult` V2가 소유한다. Winner list는 Player ID의 안정된 순서이며 Client는 유효한 result를 immutable local state에 commit한 뒤 현재 World connection의 disconnect를 요청한다.

## Ordering contract

### Join baseline

```text
TransportConnected
-> JoinWorldRequest V2
-> EntitySpawn baseline
-> ScoreState / RoundState baseline
-> WorldReady V2
-> WorldTimeSyncRequest / Response
-> active input gate open
```

- `WorldReady`는 join baseline의 마지막 record다.
- Controlled Entity Key와 일치하는 Player spawn이 먼저 staging돼야 Client가 `WorldReady`를 수락한다.
- Client는 expected Channel ID와 `WorldReady` V2의 authoritative Channel ID가 다르면 protocol fault로 처리한다.
- `WorldReady` 이후 authoritative state가 first time-sync response보다 먼저 도착할 수 있다. Client는 state를 처리할 수 있지만 required sync가 끝나기 전 movement/control 전송은 열지 않는다.
- Join preparation, baseline append 또는 binding commit 중 하나라도 실패하면 World는 준비 state를 rollback하고 connection close를 요청한다.

### Active replication

```text
authoritative commit
-> EntityRemove
-> EntitySpawn
-> EntityStateBatch
-> controlled self state와 gameplay state
```

Replication planner가 recipient와 stable Entity Key ordering을 결정하고 codec은 이미 결정된 value를 encode한다. Chunked snapshot과 overview는 partial group을 presentation state로 공개하지 않는다.

### Death와 controlled rebind

```text
old controlled EntityRemove(Destroyed)
-> Client SpawnPending
-> new Player EntitySpawn(new key)
-> ControlledEntityRebind(old key, new key)
-> 새 controlled prediction 활성화
```

Rebind의 Player ID와 이전 key는 current binding과 일치해야 하고, 새 key의 Player spawn은 같은 server tick에 먼저 staging돼야 한다. 조건이 맞지 않으면 Client는 새 entity를 controlled state로 승격하지 않는다.

## Validation과 failure 의미

| 단계 | 검증 | 실패 시 current state 의미 |
| --- | --- | --- |
| Runtime frame | transport length/version/flags와 registered packet route | gameplay payload를 World command로 해석하지 않음 |
| World codec | exact layout, payload version, known enum, finite/range value | output DTO를 성공한 값처럼 commit하지 않음 |
| World admission | session joined state, controlled generation, tick/sequence ordering | stale/late input은 drop, malformed payload는 protocol close 요청, policy violation은 reject 또는 close |
| World planning/encode | recipient, stable ordering, chunk/record invariant | partial authoritative batch를 성공한 publish처럼 계속 진행하지 않음 |
| Client decoder | packet type/version, layout, enum과 numeric value | gameplay/session state를 변경하지 않음 |
| Client session | baseline, generation, group과 lifecycle ordering | stale state는 무시할 수 있고 impossible ordering은 fault와 disconnect 요청으로 전환 |

Unknown packet type을 새 packet 의미로 추측하지 않는다. Version을 추가할 때는 기존 decoder branch를 보존하고 명시적인 새 branch를 추가한다.

## 변경 위치와 양쪽 contract

| 변경 종류 | C++ source of truth | C# mirror와 dispatch | Contract test |
| --- | --- | --- | --- |
| Packet type 추가 | [`WorldPacketTypes.h`](../../src/PrivateServer.WorldServer/WorldPacketTypes.h), 필요하면 Runtime WorldIngress catalog | `GameplayProtocol` packet type constant와 server/client dispatcher | [`WorldProtocolTests.cpp`](../../src/PrivateServer.WorldServer.Tests/WorldProtocolTests.cpp), Game Client version별 protocol tests |
| Packet field 또는 version 추가 | 해당 [`WorldServer packet header`](../../src/PrivateServer.WorldServer/)의 version namespace와 `Wire`/codec | 대응 [`Gameplay/Protocol`](../../src/PrivateServer.GameClient/Gameplay/Protocol/) value와 codec | C++/C# golden bytes, invalid length/version/enum/numeric cases |
| C2S admission 변경 | `WorldJoinIngress`, `WorldTimeSyncIngress`, `WorldMovementCommandAdmission`, `WorldControlCommandAdmission`과 [`WorldIngressPacketRouter.cpp`](../../src/PrivateServer.WorldServer/WorldIngressPacketRouter.cpp) | Client packet creation과 send gate | World ingress/admission tests와 RemoteGameplaySession tests |
| S2C ordering 변경 | World join, replication/gameplay planner와 publisher | [`ServerGameplayPacketDecoder.cs`](../../src/PrivateServer.GameClient/Gameplay/Protocol/V1/ServerGameplayPacketDecoder.cs), [`RemoteGameplaySession.cs`](../../src/PrivateServer.GameClient/Gameplay/Remote/RemoteGameplaySession.cs) | World replication/public loopback와 Client session/group tests |

Wire 변경은 C++ encode/decode만 수정해서 끝나지 않는다. C# mirror, version dispatch, golden bytes, invalid input과 session ordering contract를 같은 변경 단위에서 확인해야 한다.

## 관련 문서

- [End-to-end 게임 사이클](../end-to-end-game-cycle.md)
- [프로젝트 Source Map](../project-source-map.md)
- [NetworkRuntime Public DLL 경계](../network-runtime/public-runtime-boundary.md)
- [World Server 실행 ownership과 fixed-step pipeline](runtime-ownership-and-tick-pipeline.md)

## 지원 범위와 제약

- Protocol은 current local Channel/World gameplay를 위한 binary contract다. Account, authentication, persistence와 cross-server handoff message는 포함하지 않는다.
- Runtime transport framing과 gameplay semantic version은 서로 독립적이다.
- V1/V2/V3 번호는 packet별 layout namespace다. 모든 packet이 같은 최신 version을 가져야 한다는 뜻이 아니다.
- Codec은 AOI membership, winner 계산, simulation 결과나 presentation timing을 결정하지 않는다.
