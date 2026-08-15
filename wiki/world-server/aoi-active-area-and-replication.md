# AOI, Active Area와 Replication 계약

> Document status: Reviewed
> Baseline: c0bd3a8e5f1861c6dc1321381b6c58ca7a374030
> Last reviewed: 2026-08-16

## 핵심 답

AOI와 Active Area는 서로 다른 World 경계다. AOI는 각 Player Session의 AOI Viewpoint에 상세 replica를 보낼 entity 집합을 결정하고, Active Area는 Running round에서 player가 생존하거나 spawn할 수 있는 공간을 결정한다. Read-only Observer Session은 상세 AOI replica 대신 Channel-wide `WorldOverview`를 받는다.

```text
authoritative movement / physics / gameplay commit
-> current entity state를 spatial proxy로 투영
-> AOI enter / retain query와 visible-set diff
-> durable remove / spawn 계획
-> snapshot cadence의 self / remote state 계획
-> outbound record seal
-> NrGateway submit
```

World가 visibility와 recipient를 결정하고 Client는 `EntitySpawn`, `EntityRemove`와 snapshot group을 generation-aware replica state에 적용한다. NetworkRuntime은 AOI를 계산하지 않고 이미 결정된 recipient에게 frame을 전달한다.

## 책임과 제외 범위

이 문서는 다음을 설명한다.

- Spatial projection과 AOI query의 책임
- enter/retain hysteresis와 visible-set lifetime
- Active Area 계산, boundary death와 spawn validation
- durable lifecycle record와 replaceable snapshot의 차이
- recipient/record ordering, chunk group과 publication failure 의미

Gameplay score, round winner와 resource rule 자체는 별도 gameplay contract가 담당한다. Grid 최적화나 특정 workload의 성능 결과도 이 문서의 범위가 아니다.

## AOI와 Active Area 비교

| 경계 | 질문 | 입력 | 결과 | Owner |
| --- | --- | --- | --- | --- |
| AOI | 이 Player Session의 AOI Viewpoint가 어떤 entity의 상세 replica를 받아야 하는가? | committed entity 위치·shape, viewpoint와 이전 visible set | entered, stayed, left Entity Key | World spatial index와 AOI planner |
| Active Area | 현재 round에서 player가 생존·spawn 가능한 공간인가? | Map Bounds, round progress와 player circle/body samples | current circle, boundary death 또는 spawn rejection | World gameplay phase |
| Map Bounds | Channel World의 고정 좌표 경계는 무엇인가? | Host/World configuration | physics와 overview의 고정 arena | World configuration |

AOI 밖으로 나갔다고 entity가 죽는 것은 아니다. Active Area 밖의 player death도 Player Session의 AOI query 결과만으로 결정하지 않는다.

## Spatial projection과 index

World는 physics와 gameplay가 commit된 현재 entity state를 [`WorldSpatialProxy`](../../src/PrivateServer.WorldServer/WorldSpatialProxy.h)로 투영한다. Proxy는 World Entity Key, 중심 위치와 대표 circle radius를 가진 spatial query value이며 entity component storage 자체가 아니다.

[`WorldSpatialIndex`](../../src/PrivateServer.WorldServer/WorldSpatialIndex.cpp)는 current projection으로 uniform grid를 rebuild한다.

- `spatialCellSize`와 `aoiEnterRadius`는 양수여야 하고 `aoiRetainRadius`는 enter radius보다 커야 한다.
- Candidate cell을 찾은 뒤 최종 visibility는 AOI Viewpoint 중심과 target circle 사이의 원형 거리 조건으로 확인한다.
- AOI Viewpoint 자신의 Entity Key는 결과에서 제외한다.
- 결과는 World Entity Key 순서로 정렬하고 중복을 제거한다.
- Invalid proxy나 duplicate key 때문에 rebuild가 실패하면 이전에 commit된 index를 유지한다.

Spatial index는 replica lifetime을 소유하지 않는다. 현재 위치에 대한 query만 제공하고 recipient별 이전 visible set은 AOI planner가 소유한다.

## Enter, retain과 left

AOI는 서로 다른 enter/retain radius로 경계 진동을 줄인다.

```text
entered = enter query - previous visible
stayed  = retain query ∩ previous visible
left    = previous visible - retain query

current visible = entered ∪ stayed
```

한 번 enter한 entity는 enter radius 밖으로 이동해도 retain radius 안에 있으면 `stayed`다. Retain query에서도 빠질 때 `left`가 된다.

### Visible-set invariant

- Recipient plan은 World Session Key 순서다.
- 각 recipient의 entered, stayed와 left는 World Entity Key 순서다.
- Entity ID가 같아도 generation이 바뀌면 이전 key는 left, 새 key는 entered다.
- Authoritative gameplay removal은 visible set에서 먼저 prune한다. 이후 같은 key에 중복 `LeftAoi`를 만들지 않는다.
- Session lifecycle이 끝나면 해당 session의 visible set을 명시적으로 제거한다. 한 planning call에 recipient가 없다는 이유만으로 이전 session state를 자동 삭제하지 않는다.
- Duplicate recipient, invalid viewpoint/key 또는 inconsistent input은 계획 전체를 거절하고 이전 committed visible set과 caller output을 유지한다.

AOI planner가 commit한 visible set은 다음 tick의 diff 기준이다. Client가 임의로 distance를 다시 계산해 server visible set을 덮어쓰지 않는다.

## Active Area

Active Area는 Running round에서만 활성이다. Waiting과 Ended에서는 생존 판정 경계로 사용하지 않는다.

World는 Map Bounds 중심의 reference circle과 round 진행률로 current Active Area를 계산한다. Start/end ratio와 round duration은 [`WorldGameplayConfig`](../../src/PrivateServer.WorldServer/WorldGameplayConfig.h)가 제공하고, [`WorldActiveAreaSolver`](../../src/PrivateServer.WorldServer/WorldActiveAreaSolver.cpp)가 tick 기준으로 보간하고 유효 범위에 clamp한다.

### 생존과 spawn invariant

- Movement와 physics state를 먼저 commit한 뒤 alive Player의 head circle을 current Active Area와 비교한다.
- Player circle 전체가 Active Area 안에 있어야 한다. Boundary에 닿거나 바깥으로 나가면 outside다.
- Outside player는 authoritative boundary death set에 들어가며 protocol에서는 `EntityRemove(Destroyed)` lifecycle로 표현한다.
- Respawn candidate는 head만이 아니라 전체 body-trail sample이 current Active Area 안에 있어야 한다.
- Active Area 중심과 radius는 overview snapshot에 포함돼 Client presentation의 correction anchor가 된다.
- Client가 표시한 boundary는 시각화이며 local death authority가 아니다.

Active Area 판정은 AOI membership과 독립적이다. AOI 밖 player도 authoritative gameplay state에는 존재하며 Active Area rule을 따른다.

## Replication record 종류

Current gameplay path는 durable lifecycle과 replaceable state snapshot을 분리한다.

| 종류 | 생성 조건 | Packet | Client 의미 |
| --- | --- | --- | --- |
| AOI enter | 이전 visible set에 없던 key가 enter query에 포함됨 | `EntitySpawn` | generation-aware replica baseline 생성 |
| AOI leave | 이전 visible key가 retain query에서 빠짐 | `EntityRemove(LeftAoi)` | replica만 제거, World entity lifetime은 유지 |
| Authoritative removal | death, collection, session cleanup 또는 round reset | reason이 있는 `EntityRemove` | 해당 generation의 lifecycle 종료 |
| Self snapshot | configured snapshot cadence의 alive controlled player | `ControlledEntityState` V2 | authoritative self head/body와 control acknowledgement |
| Remote snapshot | configured snapshot cadence의 visible remote player | `EntityStateBatch` V2 | 완전한 chunk group 단위 whole-body state 교체 |
| Global overview | overview cadence | `WorldOverviewSnapshot` V3 | Map/Active Area, alive player silhouette와 leaderboard의 atomic group |

Resource와 StaticObstacle은 whole-body player snapshot 대상이 아니다. Spawn/remove lifecycle과 필요한 baseline으로 표현한다. Gameplay가 비활성인 compact World path는 stayed dynamic entity에 V1 kinematic state를 사용할 수 있다.

Initial join baseline과 `WorldReady`가 먼저 기록되고 full AOI state는 이후 replication cycle에서 이어진다. Client는 join baseline만으로 모든 remote entity가 도착했다고 가정하지 않는다.

## Ordering과 chunk atomicity

Canonical lifecycle ordering은 다음과 같다.

```text
recipient: World Session Key order
recipient 안:
  EntityRemove(World Entity Key order)
  -> EntitySpawn(World Entity Key order)
  -> EntityStateBatch(stable key order)
```

Gameplay V2 remote snapshot은 lifecycle record 뒤에 recipient 순서로 기록한다. Sealed outbound batch는 append된 record 순서를 보존해 Gateway에 제출한다.

### Chunk group invariant

- 같은 remote snapshot publication은 동일한 `snapshotId`와 server tick을 사용한다.
- 한 player whole-body record는 packet 사이에서 나누지 않는다. Packet이 나뉘면 record 사이가 chunk boundary가 된다.
- Client는 chunk index/count와 group metadata가 일치하는 complete group만 replica state로 commit한다.
- Stale 또는 이미 commit한 group은 current state를 되돌리지 않는다.
- Conflicting metadata, duplicate entity 또는 incomplete record를 정상 snapshot으로 부분 적용하지 않는다.
- Overview도 같은 원칙으로 `overviewId` group 전체가 완성된 뒤 교체한다.

Catch-up batch의 outer replication tick은 마지막 처리 tick을 나타낼 수 있지만 각 durable lifecycle payload는 원래 발생 tick을 보존한다.

## Ownership과 lifetime

| State | Mutable owner | Lifetime 경계 |
| --- | --- | --- |
| Spatial index | World replication preparation | 성공한 rebuild 사이에서 current projection 유지 |
| Recipient visible set | `WorldAoiPlanner` | Runtime Session의 World-side lifetime; session cleanup에서 제거 |
| Authoritative entity/components | World Coordinator commit path | World Entity Key generation lifetime |
| Outbound record payload | World outbound slot | write, seal, publish와 release epoch |
| Runtime frame/send | NetworkRuntime | Gateway admission부터 session actor send completion/drain |
| Client detailed replica | `RemoteReplicaStore` | transport generation과 World Entity Key lifetime |
| Client pending chunk group | group assembler | complete commit, newer replacement 또는 generation reset까지 |

Publisher는 World state나 visible set을 변경하지 않는다. 이미 완성된 recipient와 payload를 Runtime에 제출하는 publication worker다.

## Failure, pressure와 shutdown

- Spatial index rebuild 실패는 이전 committed index를 보존한다.
- AOI planning input이 invalid하면 기존 visible set을 보존한다.
- Replication publisher는 packet validity와 outbound storage 요구량을 먼저 확인한다. 전체 group을 수용할 수 없으면 그 group을 부분 append하지 않는다.
- AOI visible-set commit은 outbound record 작성보다 먼저 일어난다. 이후 outbound write가 실패하면 visible set을 되돌려 재시도하지 않고 batch를 실패로 표시한다.
- Coordinator는 실패한 outbound write batch를 정상 sealed output으로 publish하지 않고 controlled stop 경로로 전환한다.
- Outbound Publisher가 Gateway operation 자체의 실패를 받으면 해당 record에서 중단하고 sealed batch를 release한다. 자동 재시도하지 않는다.
- Multi-recipient operation이 성공하면서 일부 recipient admission을 거절한 경우 operation failure와 recipient rejection을 구분한다.
- Shutdown은 새 gameplay/output 생성 중단, 마지막 sealed output drain, terminal ingress와 worker join 순서를 따른다.

이 경계는 실패한 tick의 일부 state를 성공한 replication처럼 계속 내보내는 대신 현재 owner가 failure를 확정하고 outer lifecycle이 정리하도록 한다.

## 관련 구현과 테스트

| 독자 질문 | 관련 구현 | 관련 contract test |
| --- | --- | --- |
| Spatial proxy와 query는 어떻게 만들어지는가? | [`WorldSpatialProjectionBuilder.cpp`](../../src/PrivateServer.WorldServer/WorldSpatialProjectionBuilder.cpp), [`WorldSpatialIndex.cpp`](../../src/PrivateServer.WorldServer/WorldSpatialIndex.cpp) | [`WorldSpatialIndexTests.cpp`](../../src/PrivateServer.WorldServer.Tests/WorldSpatialIndexTests.cpp), [`WorldSpatialProjectionBuilderTests.cpp`](../../src/PrivateServer.WorldServer.Tests/WorldSpatialProjectionBuilderTests.cpp) |
| Enter/retain/left와 generation은 어떻게 계산되는가? | [`WorldAoiPlanner.cpp`](../../src/PrivateServer.WorldServer/WorldAoiPlanner.cpp) | [`WorldAoiPlannerTests.cpp`](../../src/PrivateServer.WorldServer.Tests/WorldAoiPlannerTests.cpp) |
| Active Area와 boundary death는 어디서 결정되는가? | [`WorldActiveAreaSolver.cpp`](../../src/PrivateServer.WorldServer/WorldActiveAreaSolver.cpp), [`WorldTickProcessor.cpp`](../../src/PrivateServer.WorldServer/WorldTickProcessor.cpp) | [`WorldActiveAreaSolverTests.cpp`](../../src/PrivateServer.WorldServer.Tests/WorldActiveAreaSolverTests.cpp), [`WorldTickProcessorTests.cpp`](../../src/PrivateServer.WorldServer.Tests/WorldTickProcessorTests.cpp) |
| Lifecycle record와 recipient ordering은 누가 계획하는가? | [`WorldReplicationPlan.cpp`](../../src/PrivateServer.WorldServer/WorldReplicationPlan.cpp) | [`WorldReplicationPublisherTests.cpp`](../../src/PrivateServer.WorldServer.Tests/WorldReplicationPublisherTests.cpp) |
| Whole-body snapshot과 result는 어떻게 계획되는가? | [`WorldGameplayReplicationPlan.cpp`](../../src/PrivateServer.WorldServer/WorldGameplayReplicationPlan.cpp) | [`WorldGameplayReplicationTests.cpp`](../../src/PrivateServer.WorldServer.Tests/WorldGameplayReplicationTests.cpp) |
| Sealed output은 어떤 순서로 Runtime에 제출되는가? | [`WorldOutboundPublisher.h`](../../src/PrivateServer.WorldServer/WorldOutboundPublisher.h) | [`WorldOutboundPublisherTests.cpp`](../../src/PrivateServer.WorldServer.Tests/WorldOutboundPublisherTests.cpp) |
| Client가 chunk와 generation을 어떻게 적용하는가? | `RemoteReplicaStore`와 snapshot/overview group assembler | Game Client replica, state-group와 overview-group tests |

## 관련 문서

- [Gameplay Protocol Reference](gameplay-protocol-reference.md)
- [World Server 실행 ownership과 fixed-step pipeline](runtime-ownership-and-tick-pipeline.md)
- [End-to-end 게임 사이클](../end-to-end-game-cycle.md)
- [Main thread session과 presentation lifecycle](../game-client/main-thread-session-and-presentation-lifecycle.md)

## 지원 범위와 제약

- AOI는 current single-Host Channel 안의 Player-centered detailed replication이다. Observer Session의 Channel-wide overview, Region handoff나 distributed visibility는 포함하지 않는다.
- Active Area는 round gameplay boundary이며 Map Bounds나 AOI radius를 대체하지 않는다.
- Current spatial index는 committed projection을 rebuild하는 uniform-grid 구현이다.
- Snapshot은 full authoritative state/group이며 delta compression이나 cross-packet entity record 분할을 제공하지 않는다.
- Client는 server가 제공한 detailed AOI와 overview를 소비하며 자체 visibility authority를 만들지 않는다.
