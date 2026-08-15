# World Server

> Document status: Reviewed
> Baseline: 1508dacf340e52cb4ec67e7e7a60d05755510553
> Last reviewed: 2026-08-12

## 핵심 답

World Server는 NetworkRuntime이 전달한 session event와 gameplay input을 fixed-step으로 처리하고, Channel 안의 player, resource, physics, active area, score, AOI와 replication state를 권위 있게 소유한다. 계산 결과는 World Coordinator의 canonical commit을 거친 뒤 recipient별 payload로 NetworkRuntime에 제출된다.

World Host process 하나는 Channel 하나와 독립 World instance 하나를 소유한다. 여러 Channel은 하나의 World state를 공유하지 않으며 각각 endpoint, player ID 공간, round와 entity registry를 가진다.

## 현재 제공 범위

- Runtime Session과 World player/entity binding
- server-authoritative movement와 회전
- growth point, body trail과 boost cost
- resource spawn, collision, acquisition과 score
- Map Bounds와 round 진행에 따른 Active Area
- observer AOI enter/retain과 entity spawn/state/remove replication
- round waiting/running/ended lifecycle과 RoundResult
- Channel ID/name을 포함한 authoritative join baseline
- session-scoped Player Display Name과 disconnect/rollback cleanup

## 실행 ownership

```text
NetworkRuntime To-World Event
-> World Ingress Pump: owning event를 write slot으로 이동
-> World Coordinator: sealed input과 tick/epoch 소유
-> physics/gameplay/AOI phase: typed result 계산
-> 단계별 commit: authoritative registry와 round state 변경
-> World Outbound Publisher: recipient payload를 NrGateway에 제출
-> NetworkRuntime: framing과 session별 send 수행
```

- World Ingress Pump는 payload를 해석하거나 canonical state를 변경하지 않는다.
- World Coordinator는 tick, epoch, phase 순서와 단계별 commit을 소유한다.
- World Outbound Publisher는 완성된 batch를 읽어 제출하며 World state를 수정하지 않는다.
- NetworkRuntime Session Key, Player ID와 World Entity Key는 서로 다른 identity다.

## 게임 사이클

```text
client Join
-> session/player/entity binding과 WorldReady
-> minimum player 충족
-> round running
-> input, movement, resource, score, AOI와 replication
-> configured round deadline
-> RoundResult publication
-> client disconnect와 World session/entity cleanup
-> joined player가 모두 정리되면 waiting reset
```

Game Client는 RoundResult를 local immutable result로 확정한 뒤 연결을 종료하고 Result 화면을 유지한다. 사용자가 복귀를 선택해야 초기 Channel 선택 화면으로 돌아간다. World는 disconnect와 rollback 경로에서 session binding과 display name을 함께 정리하며, ended round는 마지막 joined player가 정리된 뒤 waiting으로 reset된다. 시간만으로 자동 rematch하지 않는다.

## 관련 구현과 테스트

| 독자 질문 | 관련 구현 | 관련 테스트 |
| --- | --- | --- |
| gameplay 결과를 누가 결정하는가? | `WorldIngressEventConsumer`, `WorldTickProcessor`, `WorldGameplayCommitter` | World gameplay와 public loopback tests |
| fixed-step과 phase commit 순서는 어디서 유지되는가? | `WorldDoubleBufferedTickCoordinator` | coordinator, tick과 gameplay/physics tests |
| AOI replica와 display name은 어떤 계약으로 전달되는가? | World protocol과 outbound publisher | C++/C# protocol과 client session tests |

## 상세 문서

- [World Server 실행 ownership과 fixed-step pipeline](runtime-ownership-and-tick-pipeline.md): Host lifetime, Pump·Coordinator·Publisher 권한, A/B buffer, 단계별 commit과 shutdown 순서
- [World Host 설정과 Process Lifecycle](host-configuration-and-process-lifecycle.md): strict JSON schema, config 전달 방향, composition, stop/drain, logging과 local 실행 entrypoint
- [Gameplay Protocol Reference](gameplay-protocol-reference.md): C2S/S2C packet catalog, version·identity·ordering·validation과 C++/C# 변경 위치
- [Authoritative Gameplay와 Round 계약](authoritative-gameplay-and-round-contract.md): input admission, movement·physics·growth·resource·death·respawn·round 결과와 commit 경계
- [AOI, Active Area와 Replication 계약](aoi-active-area-and-replication.md): spatial query, enter/retain hysteresis, 생존 경계, lifecycle/snapshot ordering과 failure 의미

## 관련 근거

- `CONTEXT.md`
- `docs/adr/0004-cpp-iocp-project-baseline.md`
- `docs/adr/0006-network-runtime-world-integration-contract.md`
- `docs/design/world-server/`
- `src/PrivateServer.WorldServer/`
- `src/PrivateServer.WorldServer.Tests/`

## 지원 범위와 제약

- 현재 Channel directory와 fleet orchestration은 local 정적 설정과 PowerShell 도구다. World Manager와 동적 discovery는 포함하지 않는다.
- 여러 Channel은 process와 gameplay state가 분리되며 하나의 seamless World를 구성하지 않는다.
- account, persistence, authentication, seamless migration과 cloud scale-out은 현재 범위가 아니다.
- 이 overview는 capacity나 성능 결과를 주장하지 않는다.
