# Game Client

> Document status: Reviewed
> Baseline: c0bd3a8e5f1861c6dc1321381b6c58ca7a374030
> Last reviewed: 2026-08-16

## 핵심 답

Game Client는 Player mode에서 Channel과 일회성 profile을 선택하고 gameplay intent를 제출하며, Observer mode에서는 input authority 없이 Channel-wide overview를 표현하는 thin 2D client다. 두 mode 모두 server-authoritative state를 Godot main thread의 presentation으로 투영하며 transport, score, collision, AOI와 round 판정을 소유하지 않는다.

## 현재 게임 사이클

```text
게임 실행
-> ChannelSelect
-> Player: Channel 선택 / 선택적 닉네임 / Connect / Join / Playing
   또는 Observer: Channel 선택 / Connect / Observe / Observing
-> RoundResult local commit
-> disconnect와 transient state 정리 중 Result 화면 유지
-> 사용자 복귀 선택
-> ChannelSelect 초기 화면
```

- Channel directory에는 client가 필요한 ID, 표시 이름과 endpoint만 있다. Server config 경로는 포함하지 않는다.
- 닉네임은 영문과 숫자만 허용하고 공백을 허용하지 않는다. 빈 값은 유효하며 UI는 서버가 부여한 `PLAYER {playerId}`를 사용한다.
- Client는 선택한 Channel ID와 WorldReady가 반환한 authoritative Channel ID가 같은지 확인한다.
- 닉네임, transport generation, replica와 prediction state는 다음 입장에 자동으로 재사용하지 않는다.
- `--observe-channel <id>` launch option은 대응 Channel에 read-only Observer로 바로 접속한다. Observer는 controlled prediction이나 gameplay input을 만들지 않는다.

## Native와 presentation 경계

```text
Godot gameplay/UI
-> NetworkRuntime Managed adapter
-> C ABI opaque handle
-> native NrClient
-> TCP / World Host

native event
-> Managed owning payload
-> RemoteGameplaySession state
-> prediction / replica store
-> Godot scene와 HUD
```

Native event view는 managed owning memory로 변환한 뒤 Godot state에 전달한다. Scene과 gameplay code는 Win32 socket이나 IOCP internal type에 직접 의존하지 않는다.

## 현재 표현 범위

- local controlled body prediction과 authoritative correction
- remote entity snapshot history와 body presentation
- resource, active area boundary와 minimap
- boost visual effect와 boundary warning
- Channel과 남은 시간을 표시하는 HUD
- display name 또는 fallback Player ID를 사용하는 head label과 leaderboard
- Observer mode의 `WorldOverview` 기반 전체 player silhouette, leaderboard와 Active Area 표현
- F3 개발자 overlay를 통한 AOI와 server state 관찰

## 관련 구현과 테스트

| 독자 질문 | 관련 구현 | 관련 테스트 |
| --- | --- | --- |
| C++와 C# gameplay protocol은 어떻게 같은 wire 계약을 유지하는가? | C++ World protocol과 C# protocol codec | golden/invalid protocol tests |
| Channel과 profile state는 언제 정리되는가? | flow coordinator와 remote gameplay session | Game Client flow/session tests |
| Observer는 어떻게 자동 입장하고 read-only presentation을 유지하는가? | launch options, remote session과 `WorldOverviewPresentation` | launch option, flow, session과 result presentation tests |
| Native event payload는 managed state로 어떻게 이동하는가? | C ABI와 Managed adapter | [`Managed Smoke`](../../src/PrivateServer.NetworkRuntime.Managed.Smoke/Program.cs) |
| Authoritative state는 어떤 presentation state로 반영되는가? | remote session, prediction과 replica store | gameplay presentation tests |

## 상세 문서

- [Main thread session과 presentation lifecycle](main-thread-session-and-presentation-lifecycle.md): native event ownership, session generation, prediction·replica state와 Godot node teardown 경계

## 관련 근거

- `CONTEXT.md`
- `src/PrivateServer.NetworkRuntime.CAbi/`
- `src/PrivateServer.NetworkRuntime.Managed/`
- `src/PrivateServer.GameClient/`
- `src/PrivateServer.GameClient.Tests/`
- `tools/README.md`
- [NetworkRuntime](../network-runtime/README.md)
- [World Server](../world-server/README.md)

## 지원 범위와 제약

- Client는 서버 동작을 보여주는 표현 계층이며 gameplay authority를 소유하지 않는다.
- Observer 창은 촬영·시각 검증용 추가 session과 렌더링 부하를 만들며 canonical benchmark 결과를 대표하지 않는다.
- Channel directory는 local 정적 JSON이다. Population, health, matchmaking과 dynamic discovery는 포함하지 않는다.
- 닉네임은 account나 persistent identity가 아닌 한 World session의 display value다.
- Godot export pipeline과 배포 패키지는 현재 개발 실행 및 headless smoke와 별도 범위다.
