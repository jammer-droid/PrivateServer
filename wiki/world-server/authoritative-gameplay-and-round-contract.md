# Authoritative Gameplay와 Round 계약

> Document status: Reviewed
> Baseline: 1508dacf340e52cb4ec67e7e7a60d05755510553
> Last reviewed: 2026-08-12

## 핵심 답

Client는 movement와 control 의도를 보낼 뿐 위치, 충돌, growth, resource 획득, death, respawn, score와 round 결과를 확정하지 않는다. World Coordinator가 fixed-step 안에서 입력을 admission하고 movement/physics 결과와 gameplay result를 단계별로 commit해 canonical state를 만든다.

```text
joined session의 input
-> generation·tick·sequence admission
-> movement/control input 확정
-> physics compute
-> movement state commit과 cadence 기반 body sample
-> Active Area / collision death 판정과 기존 SpawnPending의 respawn candidate 계획
-> death / boost cost / resource pickup / round transition 계산
-> gameplay와 spawn commit
-> body dimensions finalize
-> lifecycle / score / round / snapshot replication 계획
```

Movement, physics와 gameplay의 주요 compute phase는 immutable input 또는 read view에서 typed result를 만든다. Canonical mutation은 Coordinator-owned 실행 경로의 명시적 apply/committer 단계에서 직렬화되며, control admission이나 component replacement처럼 전용 apply 경계를 사용하는 mutation도 이 실행 경로 밖에서 병렬 수행하지 않는다. Physics scene은 판정 결과를 반환하지만 World entity를 직접 소유하거나 수정하지 않는다.

## 책임과 제외 범위

이 문서는 다음을 설명한다.

- gameplay input의 admission과 authoritative 적용 순서
- movement, physics, body, growth와 boost의 관계
- resource arbitration, score, death drop과 respawn 규칙
- round 시작·종료·result·reset 계약
- 단계별 commit과 실패 시 rollback 범위

Packet field와 version은 [Gameplay Protocol Reference](gameplay-protocol-reference.md), visibility와 replica ordering은 [AOI, Active Area와 Replication 계약](aoi-active-area-and-replication.md)이 담당한다. Rendering, prediction과 result 화면은 [Game Client](../game-client/README.md)의 책임이다.

## 권위 상태와 identity

| State | Mutable owner | Identity / lifetime |
| --- | --- | --- |
| Runtime session과 player binding | `WorldSessionRegistry` | Runtime Session Key는 accepted connection의 World-side lifetime을 식별하고, join commit부터 cleanup까지 Player ID·controlled Entity Key·display name binding을 보관 |
| Player와 resource entity | `WorldEntityManager` | `WorldEntityKey(entityId, generation)`이 entity lifetime을 식별 |
| Movement/control command | `WorldMovementCommandStore`와 Player control component | session binding과 controlled generation, target tick 또는 input sequence로 admission |
| Score, lifecycle, round와 resource slot | `WorldGameplayState` | Player ID와 Round ID의 World-owned lifetime |
| Movement/static contact와 resource trigger fact | `WorldPhysicsStepResult` | 해당 simulation tick의 immutable 계산 결과 |
| Player collision death fact | Tick processor의 collision death set | physics scene의 player collision query를 resolver가 해당 tick의 death candidate로 정규화 |
| Client replica와 prediction | Game Client session/presentation | authoritative state의 소비자이며 gameplay authority가 아님 |

Accepted-but-unjoined session도 registry에 존재하지만 Player ID나 controlled entity binding은 없다. Join은 Player ID와 controlled Entity Key를 준비하고 baseline을 기록한 뒤 binding을 commit한다. Baseline append나 commit이 실패하면 준비한 state를 rollback하고 connection close를 요청한다. Join 이후 input은 payload 안의 임의 Player ID가 아니라 Runtime Session binding으로 actor를 찾는다.

## Input admission

### MovementInput

`MovementInput`은 current joined session과 controlled generation이 일치해야 한다. Target-tick mode에서는 `targetServerTick`의 window와 같은 tick의 중복을 검사하고, double-buffered mode에서는 sealed ingress batch가 tick input의 경계를 제공한다.

Stale generation과 늦은 command는 현재 entity를 변경하지 않는다. 허용 범위보다 앞선 target이나 반복되는 policy 위반은 정상 movement로 적용하지 않으며 session close policy로 이어질 수 있다.

### ControlStateCommand

`ControlStateCommand`는 current controlled generation과 마지막 적용 sequence보다 새로운 `inputSequence`를 요구한다. Turn과 boost는 허용된 enum만 admission하며, accepted state는 Player entity의 control component에 저장된다.

Control movement가 활성화된 실행에서는 [`WorldTickProcessor`](../../src/PrivateServer.WorldServer/WorldTickProcessor.cpp)가 joined/alive player의 control component에서 해당 tick의 movement input vector를 새로 만들며, command store의 `MovementInput` vector에 더하지 않고 교체한다. Control movement를 사용하지 않는 fallback 실행에서만 command store의 movement vector를 그대로 physics에 전달한다.

Movement solver는 tick 시작 시점의 authoritative growth와 control state에서 heading, direction과 speed를 계산한다. Boost를 요청해도 growth가 없으면 boost speed를 사용하지 않는다. 같은 tick에서 뒤에 차감될 boost cost를 movement speed 계산에 소급 적용하지 않는다.

## Movement, physics와 body

Tick processor는 선택된 movement input과 tick 시작 시점의 growth/body geometry를 [`WorldPhysicsScene`](../../src/PrivateServer.WorldServer/WorldPhysicsScene.cpp)에 player motion, static solid와 resource trigger proxy로 전달한다.

- Static obstacle은 solid collision과 sweep-and-slide 결과에 참여한다.
- Resource는 movement를 막지 않는 trigger이며 최종 overlap이 pickup candidate가 된다.
- Player head/body contact는 movement가 commit된 뒤 player collision death set으로 해석한다.
- Physics result는 계산 사실이며 transform과 motion의 canonical 변경은 `WorldMovementPhaseCommitter`가 수행한다.
- Movement commit 뒤 configured body-sample cadence가 도래하면 현재 head 위치를 trail에 기록한다. 이 tick의 collision geometry는 tick 시작 growth를 사용하며, boost나 pickup이 growth를 바꾼 뒤 gameplay commit이 canonical body 길이와 diameter를 다시 finalize한다.

Player collision에서 서로 다른 owner의 head끼리 닿으면 양쪽 head owner가 death candidate다. Head와 다른 player body의 contact에서는 head owner가 death candidate다. 같은 owner의 self contact는 제외하며 body끼리만 이루어진 contact는 valid death input이 아니다.

## Growth, boost와 resource

현재 `score`는 별도 combat point가 아니라 player의 growth point다. [`WorldGrowthSolver`](../../src/PrivateServer.WorldServer/WorldGrowthSolver.cpp)가 growth point를 body nominal length와 diameter로 투영하고, body trail은 그 결과에 맞게 trim된다.

Boost cost는 fixed delta 동안 누적한 fractional cost를 growth point 차감으로 변환한다. Growth는 음수가 되지 않으며 death로 `SpawnPending`이 된 player에는 같은 tick의 boost cost나 pickup award를 적용하지 않는다.

Resource pickup은 physics trigger overlap을 다음 순서로 정규화한다.

1. resource와 player identity, registry와 fixture 관계를 검증한다.
2. candidate를 Resource Entity Key, Player Entity Key의 안정된 순서로 정렬한다.
3. 같은 resource의 첫 candidate만 winner로 선택한다.
4. 선택된 resource를 제거하고 Player ID별 award를 합산한다.
5. full authoritative score snapshot을 replication 대상으로 만든다.

한 player가 서로 다른 resource를 같은 tick에 획득할 수 있지만, 하나의 resource는 하나의 player에게만 귀속된다. Active Area 밖으로 밀려난 resource는 population reconcile에서 제거된다. Ambient target은 hard maximum이 아니라 부족할 때 채우는 refill minimum이다. Death drop은 ambient slot을 사용하지 않는 별도 origin이지만 전체 projected resource count에는 포함되므로 ambient refill을 억제할 수 있다.

Running tick의 gameplay precedence는 다음과 같다.

```text
movement / physics commit
-> Active Area boundary와 collision death 확정
-> 죽은 player를 boost와 pickup 대상에서 제외
-> boost cost
-> resource pickup과 score
-> resource cleanup과 ambient refill
-> round transition과 result planning
-> canonical body dimensions finalize
```

## Death, drop과 respawn

Running round의 death cause는 collision과 Active Area boundary를 bit flags로 합칠 수 있다. Death commit은 이전 Player entity를 제거하고 score와 boost accumulator를 초기화하며 player lifecycle을 `SpawnPending`으로 전환한다.

Death drop은 현재 다음 경우에만 생성한다.

- cause가 정확히 collision인 player death
- Running round에서 alive player가 disconnect하며 보존한 growth snapshot

Boundary-only death와 collision/boundary가 동시에 성립한 death에는 drop을 만들지 않는다. Drop placement는 source head/body trail, current Active Area와 이미 예약된 resource 위치를 사용하며 배치할 수 없는 일부 candidate를 정상 resource로 강제 생성하지 않는다.

Death tick에 `SpawnPending`으로 바뀐 player는 다음 simulation tick부터 respawn 대상이 된다. Respawn planner는 pending player를 Player ID 순서로 처리하고 deterministic candidate를 만든다. Candidate의 전체 body가 Map Bounds와 current Active Area 안에 있어야 하며, 현재 alive player collision proxy와 같은 tick에 이미 예약한 spawn bounds와 겹치지 않아야 한다. 현재 respawn placement는 static obstacle을 별도 검사하지 않는다. Spawn commit은 새 World Entity Key의 Player entity를 생성하고 같은 session의 controlled binding을 새 key로 교체한다.

Client에 보이는 lifecycle 순서는 다음과 같다.

```text
old EntityRemove(Destroyed)
-> new EntitySpawn(new Entity Key)
-> ControlledEntityRebind(old key, new key)
```

새 spawn baseline이 먼저 준비되지 않으면 Client는 rebind만으로 새 controlled entity를 만들지 않는다.

## Round 계약

### 시작

World는 `Waiting`에서 joined player 수가 `minimumPlayersToStart`를 충족하면 같은 Round ID로 `Running`을 시작한다. Waiting에서도 join, movement/control admission과 movement simulation은 진행된다. 시작 commit은 score를 초기화하고 initial Active Area 안의 ambient resource population을 만든다.

### 진행과 종료

Running tick에서 movement와 gameplay가 진행되며 current round deadline에 도달하면 `Ended`로 전이한다. Deadline tick도 death, boost, pickup과 score를 먼저 commit한 뒤 transition과 `RoundResult`를 계획하므로 final outcome은 end-tick post-commit state를 기준으로 한다. `scoreToWin`은 protocol/config의 score contract metadata지만 현재 구현에서 조기 종료 조건으로 사용하지 않는다. `endedDurationTicks`도 자동 rematch timer로 사용하지 않는다.

Ended에서는 새 join과 일반 gameplay packet을 받지 않고 simulation result를 더 진행하지 않는다. Time-sync request는 Ended gate보다 먼저 처리되므로 응답할 수 있다. 전이 tick에는 `RoundResult`를 현재 joined session별로 한 번 계획한다. Winner는 result 계산 시점에 connected이며 alive인 player 중 가장 높은 growth point를 가진 Player ID 집합이고, 동점은 Player ID 순서로 모두 포함한다. `SpawnPending` player는 winner 후보에서 제외되지만 해당 recipient의 final growth는 결과에 남는다. Eligible player가 없으면 winner list는 비어 있다.

`RoundState`의 single-winner field는 compatibility state이며 공동 winner와 winner 없음까지 포함하는 final outcome은 `RoundResult`가 소유한다. Ended 전이는 별도 Ended `RoundState` broadcast 대신 final result publication으로 표현한다.

### Reset

시간만으로 새 round를 시작하지 않는다. Ended round의 joined player가 disconnect cleanup으로 모두 제거되면 Round ID를 전진시키고 `Waiting`으로 reset한다. Client는 `RoundResult`를 immutable local result로 확정하고 연결을 정리한 뒤, 사용자가 복귀를 선택할 때 Channel 선택 화면으로 돌아간다.

## Commit과 failure 의미

World tick 전체는 하나의 global transaction이 아니다. 다음과 같은 local transaction/commit 경계를 조합한다.

| 경계 | 성공 전 검증 | 실패 시 보존 범위 |
| --- | --- | --- |
| Join | session state, Player/Entity 준비와 baseline append | 준비한 binding/entity/gameplay state를 rollback하고 close 요청 |
| Movement/physics | input set, projection, physics result와 entity generation | 계산 실패 전에는 movement state를 commit하지 않음 |
| Gameplay | death, resource, score, round와 spawn request의 cross-reference | 검증 뒤 canonical registries를 단계적으로 변경하며 이전 gameplay mutation 전체를 되돌리는 strong rollback은 제공하지 않음 |
| Resource / Player spawn | slot 또는 pending lifecycle, session binding과 candidate collision | 생성 준비가 실패한 대상에는 targeted rollback을 적용하고 이미 확정된 다른 gameplay mutation은 유지 |
| Outbound record | recipient, encode와 batch storage | 이미 commit된 World state를 rollback하지 않고 controlled stop으로 전환 |

Movement commit 뒤 body sample이나 gameplay 계산이 실패하면 앞서 commit한 movement를 이전 tick으로 되돌리는 global rollback은 없다. Resource 또는 Player spawn 생성 경계의 targeted rollback을 제외하면, 뒤 단계 실패가 앞서 적용된 gameplay mutation을 되돌리지 않는다. Coordinator는 실패한 결과를 정상 replication처럼 계속 publish하지 않고 worker/Host lifecycle에 failure를 전달한다.

## 변경 위치와 contract tests

| 변경 질문 | 구현 시작점 | Contract test |
| --- | --- | --- |
| movement/control admission과 speed는 어디서 바꾸는가? | [`WorldMovementCommandAdmission.cpp`](../../src/PrivateServer.WorldServer/WorldMovementCommandAdmission.cpp), [`WorldControlCommandAdmission.cpp`](../../src/PrivateServer.WorldServer/WorldControlCommandAdmission.cpp), [`WorldControlMovementSolver.cpp`](../../src/PrivateServer.WorldServer/WorldControlMovementSolver.cpp) | [`WorldMovementCommandAdmissionTests.cpp`](../../src/PrivateServer.WorldServer.Tests/WorldMovementCommandAdmissionTests.cpp), [`WorldControlCommandAdmissionTests.cpp`](../../src/PrivateServer.WorldServer.Tests/WorldControlCommandAdmissionTests.cpp), [`WorldControlMovementSolverTests.cpp`](../../src/PrivateServer.WorldServer.Tests/WorldControlMovementSolverTests.cpp) |
| physics, collision과 movement commit은 어디서 바꾸는가? | [`WorldTickProcessor.cpp`](../../src/PrivateServer.WorldServer/WorldTickProcessor.cpp), [`WorldPhysicsScene.cpp`](../../src/PrivateServer.WorldServer/WorldPhysicsScene.cpp), [`WorldCollisionDeathResolver.cpp`](../../src/PrivateServer.WorldServer/WorldCollisionDeathResolver.cpp) | [`WorldTickProcessorTests.cpp`](../../src/PrivateServer.WorldServer.Tests/WorldTickProcessorTests.cpp), [`WorldPhysicsSceneTests.cpp`](../../src/PrivateServer.WorldServer.Tests/WorldPhysicsSceneTests.cpp), [`WorldCollisionDeathResolverTests.cpp`](../../src/PrivateServer.WorldServer.Tests/WorldCollisionDeathResolverTests.cpp) |
| growth, boost와 body는 어디서 바꾸는가? | `WorldGrowthSolver`, `WorldBoostCostSolver`, `WorldPlayerBody`와 body sampler/trimmer | [`WorldGrowthSolverTests.cpp`](../../src/PrivateServer.WorldServer.Tests/WorldGrowthSolverTests.cpp), [`WorldBoostCostSolverTests.cpp`](../../src/PrivateServer.WorldServer.Tests/WorldBoostCostSolverTests.cpp), [`WorldPlayerBodyTests.cpp`](../../src/PrivateServer.WorldServer.Tests/WorldPlayerBodyTests.cpp) |
| resource, death drop과 round rule은 어디서 바꾸는가? | [`WorldGameplayPhase.cpp`](../../src/PrivateServer.WorldServer/WorldGameplayPhase.cpp), [`WorldResourceSpawnPlanner.cpp`](../../src/PrivateServer.WorldServer/WorldResourceSpawnPlanner.cpp), [`WorldRoundResultPlanner.cpp`](../../src/PrivateServer.WorldServer/WorldRoundResultPlanner.cpp) | [`WorldGameplayPhaseTests.cpp`](../../src/PrivateServer.WorldServer.Tests/WorldGameplayPhaseTests.cpp), [`WorldResourceSpawnPlannerTests.cpp`](../../src/PrivateServer.WorldServer.Tests/WorldResourceSpawnPlannerTests.cpp), [`WorldRoundResultPlannerTests.cpp`](../../src/PrivateServer.WorldServer.Tests/WorldRoundResultPlannerTests.cpp) |
| canonical gameplay/spawn commit과 rebind는 어디서 바꾸는가? | [`WorldGameplayCommitter.cpp`](../../src/PrivateServer.WorldServer/WorldGameplayCommitter.cpp), [`WorldIngressEventConsumer.cpp`](../../src/PrivateServer.WorldServer/WorldIngressEventConsumer.cpp) | [`WorldGameplayCommitterTests.cpp`](../../src/PrivateServer.WorldServer.Tests/WorldGameplayCommitterTests.cpp), [`WorldPlayerSpawnPlannerTests.cpp`](../../src/PrivateServer.WorldServer.Tests/WorldPlayerSpawnPlannerTests.cpp), [`WorldGameplayReplicationTests.cpp`](../../src/PrivateServer.WorldServer.Tests/WorldGameplayReplicationTests.cpp), [`WorldIngressEventConsumerTests.cpp`](../../src/PrivateServer.WorldServer.Tests/WorldIngressEventConsumerTests.cpp) |

## 관련 문서

- [Gameplay Protocol Reference](gameplay-protocol-reference.md)
- [AOI, Active Area와 Replication 계약](aoi-active-area-and-replication.md)
- [World Server 실행 ownership과 fixed-step pipeline](runtime-ownership-and-tick-pipeline.md)
- [End-to-end 게임 사이클](../end-to-end-game-cycle.md)

## 지원 범위와 제약

- 현재 gameplay는 단일 Channel/Host 안에서 하나의 Coordinator가 mutation을 직렬화한다.
- Collision, score, round와 respawn authority는 Client에 위임하지 않는다.
- Round는 deadline 기반이며 score threshold 조기 종료와 자동 rematch를 제공하지 않는다.
- Death drop은 collision-only death와 Running disconnect snapshot에 한정된다.
- Tick phase가 실패하면 이전 phase의 canonical commit을 되돌리는 global rollback을 제공하지 않는다.
