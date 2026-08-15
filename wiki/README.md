# Private Server Wiki

> Document status: Reviewed
> Baseline: c0bd3a8e5f1861c6dc1321381b6c58ca7a374030
> Last reviewed: 2026-08-16

이 Wiki는 Private Server의 현재 구조, interface, ownership, lifetime와 runtime behavior를 설명한다. 처음 보는 개발자나 에이전트는 이 페이지에서 전체 경계를 확인하고, 독자 질문에 맞는 문서와 구현 source로 이동할 수 있다.

## 현재 프로젝트 기준선

Private Server는 Windows IOCP 기반 NetworkRuntime, Channel별 World Host, server-authoritative World와 thin Godot Game Client를 하나의 실행 가능한 MMO-lite 게임 사이클로 연결한다.

```text
Channel 선택
-> Player: 일회성 profile 입력 / Join
   또는 Observer: read-only Observe
-> authoritative fixed-step gameplay
-> Player AOI replication / Observer World overview
-> Godot prediction 또는 read-only presentation
-> RoundResult
-> connection과 session state 정리
-> Result 화면에서 사용자가 복귀 선택
-> Channel 선택 화면 복귀
```

World Host process 하나는 Channel 하나와 독립 World instance 하나를 소유한다. Local fleet는 여러 Host를 서로 다른 endpoint에서 실행하며 Channel 간 mutable gameplay state를 공유하지 않는다.

## 시스템 경계

```text
Godot Game Client
-> Managed / C ABI / native NrClient
-> TCP / IOCP NetworkRuntime
-> World ingress와 authoritative fixed-step simulation
-> AOI와 recipient별 replication
-> NetworkRuntime send
-> Game Client prediction / presentation
```

- NetworkRuntime은 connection, framing, Runtime Session과 pending I/O lifetime을 소유한다.
- World Server는 Channel 안의 session role, player/entity binding, gameplay, AOI와 replication 결정을 소유한다.
- World Host는 Runtime과 World의 configuration, worker와 process lifetime을 조립한다.
- Game Client는 Player mode에서 server-authoritative state를 local prediction과 replica로 투영하고, Observer mode에서는 World overview를 read-only Godot presentation으로 표현한다.

## 문서 구조

```text
wiki/
|-- .wiki-documents
|-- README.md
|-- end-to-end-game-cycle.md
|-- project-source-map.md
|-- system-architecture.md
|-- network-runtime/
|   |-- README.md
|   |-- public-runtime-boundary.md
|   `-- session-actor-ownership-and-io-lifetime.md
|-- world-server/
|   |-- README.md
|   |-- host-configuration-and-process-lifecycle.md
|   |-- runtime-ownership-and-tick-pipeline.md
|   |-- gameplay-protocol-reference.md
|   |-- authoritative-gameplay-and-round-contract.md
|   `-- aoi-active-area-and-replication.md
`-- game-client/
    |-- README.md
    `-- main-thread-session-and-presentation-lifecycle.md
```

## 독자 질문별 시작점

| 알고 싶은 내용 | 시작 문서 |
| --- | --- |
| 실행 process, library와 dependency는 어떻게 연결되는가? | [전체 시스템 아키텍처](system-architecture.md) |
| 어떤 변경을 어느 source·API·test에서 시작해야 하는가? | [프로젝트 Source Map](project-source-map.md) |
| 접속부터 RoundResult와 cleanup까지 어떻게 이어지는가? | [End-to-end 게임 사이클](end-to-end-game-cycle.md) |
| Player와 read-only Observer session은 admission과 replication이 어떻게 다른가? | [Gameplay Protocol Reference](world-server/gameplay-protocol-reference.md) |
| IOCP Runtime의 public contract와 내부 구현 경계는 무엇인가? | [NetworkRuntime](network-runtime/README.md) |
| Host config는 어디서 Runtime·World graph가 되고 process는 어떻게 정지하는가? | [World Host 설정과 Process Lifecycle](world-server/host-configuration-and-process-lifecycle.md) |
| authoritative fixed-step owner와 A/B worker pipeline은 무엇인가? | [World Server 실행 ownership과 fixed-step pipeline](world-server/runtime-ownership-and-tick-pipeline.md) |
| gameplay packet type, wire field와 version은 어디서 바꾸는가? | [Gameplay Protocol Reference](world-server/gameplay-protocol-reference.md) |
| movement, resource, death, respawn과 round rule은 무엇인가? | [Authoritative Gameplay와 Round 계약](world-server/authoritative-gameplay-and-round-contract.md) |
| Active Area, AOI와 recipient별 replication은 어떻게 다른가? | [AOI, Active Area와 Replication 계약](world-server/aoi-active-area-and-replication.md) |
| native event가 prediction과 Godot presentation으로 어떻게 이동하는가? | [Game Client](game-client/README.md) |

## Subsystem 책임

| Subsystem | 입력 | 출력 | 소유하지 않는 것 |
| --- | --- | --- | --- |
| [NetworkRuntime](network-runtime/README.md) | socket I/O, public submit과 lifecycle operation | owning Runtime event, framed socket output, status와 snapshot | gameplay rule, World entity와 scene state |
| [World Server](world-server/README.md) | Runtime session event와 gameplay command | authoritative state, AOI recipient와 semantic payload | socket framing, send completion과 client presentation |
| [Game Client](game-client/README.md) | user intent와 authoritative Runtime event | input packet, local prediction, replica와 visual state | collision·score·round·AOI authority |

## 지원 범위와 제약

- 현재 배치는 Host process 하나당 Channel 하나와 독립 World instance 하나다.
- Channel directory와 fleet orchestration은 local configuration이며 dynamic discovery나 matchmaking을 제공하지 않는다.
- Client는 thin presentation layer이며 offline gameplay authority를 제공하지 않는다.
- Production MMORPG 규모의 seamless World, account/persistence, seamless migration과 cloud orchestration은 현재 범위가 아니다.
- 성능과 capacity는 별도 workload와 환경에서 검증해야 하며 이 Wiki의 구조 설명 자체가 특정 결과를 보장하지 않는다.
