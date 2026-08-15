# Session Actor ownership과 I/O lifetime

> Document status: Reviewed
> Baseline: d69af93f1b8a614da325fef08b6c68919fbbd694
> Last reviewed: 2026-08-10

## 핵심 답

NetworkRuntime은 연결마다 `NrSessionIoActor`를 두고 socket, receive buffer, pending recv/send context, outbound payload queue와 close 상태를 한 actor 안에 모은다. IOCP worker는 이 상태를 직접 변경하지 않는다. Completion을 actor mailbox event로 바꾸고 scheduler에 넘기며, schedule gate가 허용한 drain owner만 session state를 변경한다.

객체 수명과 실행 권한은 다음처럼 나뉜다.

```text
NrServer
-> server component graph
   -> actor registry
      -> session actor
         -> session / socket / receive buffer
         -> pending recv context lease
         -> pending send context lease + owning payload
         -> accept-recv mailbox / send mailbox
         -> send-channel control

IOCP completion
-> I/O event dispatcher
-> actor mailbox admission
-> ready queue
-> actor executor
-> single actor drain owner
```

Registry가 actor 객체의 lifetime owner이고, actor lease는 executor가 사용하는 동안 registry 삭제를 막는다. Actor는 pending I/O context lease를 session 안에 보관하므로 `OVERLAPPED`와 전송 payload가 completion 처리 전에 사라지지 않는다.

## 책임과 제외 범위

이 문서는 다음 질문을 다룬다.

- accept된 socket은 언제 Runtime Session Actor의 소유가 되는가?
- IOCP completion은 어떻게 session별 직렬 실행으로 전달되는가?
- recv buffer와 outbound payload는 어느 owner 아래에서 유효한가?
- close 요청 뒤 late completion과 actor 제거는 어떻게 처리되는가?
- server stop/shutdown은 새 work admission과 component lifetime을 어떤 순서로 닫는가?

다음 내용은 별도 문서의 책임이다.

- 제품 소비자가 의존하는 DLL과 C ABI 경계
- World Server의 fixed-step simulation과 gameplay state
- Game Client의 presentation lifecycle
- capacity와 성능 결과

## Lifetime owner와 mutation owner

| 대상 | Lifetime owner | Mutable state 변경 권한 |
| --- | --- | --- |
| server component graph | `NrServer::Impl` | public lifecycle operation |
| actor registry entry와 session actor | `NrSessionActorRegistry` | registry operation과 lease를 얻은 executor |
| socket, receive buffer, pending I/O lease | `NrSessionIoActor`가 보유한 `NrSession` | actor drain 경로 |
| accept/recv mailbox와 send mailbox | session actor | scheduler admission이 event를 기록하고 actor drain이 소비 |
| send-channel control | reference-counted control object | actor가 close 상태를 게시하고 Gateway가 open 상태에서만 submit |
| IOCP context memory | pending context lease | context factory가 만들고 matching completion 처리 뒤 session이 해제 |
| To-World event payload | `NrToWorldHandoff`를 거쳐 `NrToWorldEvent` | Runtime이 publish하고 World가 owning event 수명 안에서 decode |

`NrSessionActorRegistry`의 lease는 raw actor pointer를 독립적으로 보관하는 대신 registry entry의 active use를 기록한다. Registry는 active lease가 있거나 actor가 pending I/O를 보유하면 일반 close 경로에서 해당 entry를 제거하지 않는다.

## Accept에서 첫 recv까지

[`NrIoEventDispatcher`](../../src/PrivateServer.NetworkRuntime.Internal/NrIoEventDispatcher.cpp)는 accept completion을 받으면 다음 순서로 session 실행 단위를 만든다.

1. 새 Runtime Session Key를 할당한다.
2. listener가 accepted socket을 IOCP에 연결하고 accept context에서 socket을 넘긴다.
3. To-World handoff에 해당 session의 lifecycle publication 자리를 먼저 예약한다.
4. socket과 receive buffer를 소유하는 `NrSession`을 만든다.
5. 두 mailbox, pending send queue와 send-channel control을 가진 `NrSessionIoActor`를 만든다.
6. Actor를 registry에 등록하고 `Accepted` event를 scheduler에 admission한다.

Actor는 `Accepted` event를 drain하면서 `SessionAccepted`를 To-World handoff에 먼저 기록한 뒤 첫 recv posting을 요청한다. 따라서 첫 recv posting이 실패하더라도 World가 관찰하는 lifecycle은 accepted 뒤 closed 순서를 유지한다.

Bootstrap이 registry 공개 전에 실패하면 예약을 취소한다. Registry 등록 뒤 scheduling이 실패하면 close와 deregistration을 요청하고 예약을 정리한다. 이 순서는 World lifecycle slot과 actor 객체가 서로 다른 성공 상태로 남지 않도록 한다.

## Completion에서 actor drain까지

IOCP worker가 받은 `OVERLAPPED`는 [`NrIocpIoCompletionDispatcher`](../../src/PrivateServer.NetworkRuntime.Internal/NrIocpIoCompletionDispatcher.cpp)에서 context header의 operation type에 따라 accept, recv 또는 send event로 변환된다.

Recv와 send completion에는 다음 identity가 함께 전달된다.

- Runtime Session Key
- 전송된 byte 정보와 operation status
- active context 주소에서 얻은 context token

[`NrIoEventDispatcher`](../../src/PrivateServer.NetworkRuntime.Internal/NrIoEventDispatcher.cpp)는 completion을 해당 actor의 mailbox에 admission한다. [`NrActorScheduleGate`](../../src/PrivateServer.NetworkRuntime.Internal/NrActorScheduleGate.h)는 mailbox commit, ready token과 drain 상태를 하나의 scheduling protocol로 묶고, [`NrActorExecutor`](../../src/PrivateServer.NetworkRuntime.Internal/NrActorExecutor.cpp)는 drain 시작 권한을 얻은 실행자만 actor를 호출한다.

Actor drain은 accept/recv mailbox와 send mailbox를 번갈아 소비한다. 한 drain에서 처리할 work는 bounded budget을 사용하고, 남은 work가 있으면 ready queue에 다시 게시한다. 이 구조는 같은 actor를 여러 worker가 동시에 변경하지 않으면서 다른 session의 실행 기회를 보존한다.

## Receive ownership과 publication

[`NrSessionIoOperations::PostRecv`](../../src/PrivateServer.NetworkRuntime.Internal/NrSessionIoOperations.cpp)는 session의 writable receive buffer를 가리키는 pooled context lease를 만든 뒤 session에 pending recv로 먼저 기록하고 `WSARecv`를 호출한다. Native posting이 실패하면 pending lease를 즉시 해제한다.

Matching recv completion을 drain할 때 actor는 다음 순서를 사용한다.

1. Session Key, pending recv 존재 여부와 context token을 확인한다.
2. Completion이 현재 pending context에 속하면 받은 bytes를 receive buffer에 commit한다.
3. 완성된 frame을 순서대로 parse하고 dispatch rule을 찾는다.
4. World ingress packet은 payload를 To-World handoff가 소유하는 event로 복사해 publish한다.
5. Admission이 성공한 frame만 receive buffer에서 소비한다.
6. Close 요청이 없다면 다음 recv posting을 요청한다.

Incomplete frame은 buffer에 남는다. Protocol 오류, publication pressure 또는 transport 실패는 session close 사유가 된다. World handoff나 input queue admission이 실패한 frame은 성공한 것처럼 consume하지 않는다.

Close가 이미 요청된 상태에서 도착한 matching recv completion은 context와 pending 상태를 정리하는 용도로만 처리한다. 받은 bytes를 새 application input으로 publish하거나 다음 recv를 post하지 않는다.

## Send ownership과 partial completion

World는 public `NrGateway`와 `NrSessionSendChannel`을 사용해 semantic payload를 제출한다. Gateway는 server submission permit을 얻고 wire frame을 Runtime-owned immutable payload로 만든 다음 actor send mailbox에 `SendRequested` event를 admission한다.

Actor는 outbound work를 다음 규칙으로 처리한다.

- Mailbox에서 받은 owning payload를 actor의 pending send queue로 이동한다.
- Active pending send가 없을 때만 queue의 다음 payload를 꺼내 send context lease를 만든다.
- Send context가 owning payload reference와 현재 전송 위치를 함께 보관한다.
- Partial completion이면 전송 위치를 전진시키고 남은 span으로 같은 context를 repost한다.
- Frame 전송이 끝난 뒤 context를 해제하고 다음 queued payload를 시작한다.

따라서 caller의 원본 bytes와 World outbound slot은 Gateway submit이 성공한 뒤 socket completion까지 살아 있을 필요가 없다. 실제 전송 lifetime은 actor의 immutable payload reference와 pending send context가 소유한다.

Send queue admission pressure, posting 실패, zero-byte completion, foreign completion 또는 context mismatch는 close 경로로 전환한다. Close 뒤 도착한 matching send completion은 active context를 해제하지만 남은 bytes를 repost하거나 다음 queued send를 시작하지 않는다.

## Close와 actor 회수

Close는 socket 파괴와 actor 삭제를 즉시 같은 operation으로 수행하지 않는다. [`NrSessionIoActor::RecordCloseRequested`](../../src/PrivateServer.NetworkRuntime.Internal/NrSessionIoActor.cpp)는 첫 close reason을 보존하고 다음 전이를 시작한다.

```text
close requested
-> send channel closed: 새 Gateway submit 거절
-> socket close: pending native I/O가 completion 경로로 돌아오게 함
-> SessionClosed를 To-World handoff에 한 번 게시
-> 새 recv/send posting 중단
-> matching late completion으로 pending context 정리
-> pending recv/send가 없으면 close ready
-> active actor lease가 사라지면 registry entry 제거 가능
```

Context identity가 맞지 않는 completion은 현재 pending context를 임의로 해제하지 않는다. Actor invariant 위반으로 close를 요청하고 containment 경로를 유지한다.

Registry의 일반 deregistration 조건은 actor lifecycle이 더 이상 active가 아니고, active lease가 없으며, actor에 pending I/O가 없는 것이다. 이 조건 때문에 executor가 actor를 사용하는 중이거나 native completion이 참조할 context가 남아 있을 때 entry가 먼저 파괴되지 않는다.

## Server stop과 graph shutdown

Session close와 전체 server shutdown은 같은 범위의 operation이 아니다.

`NrServer::RequestStop`은 먼저 submission gate를 invalidate하고 진행 중인 admission이 permit을 반납할 때까지 기다린다. 그 뒤 component graph에 reverse-order stop을 전달한다. 이미 발급된 Gateway와 send channel은 안전한 value로 남을 수 있지만 새 Runtime work를 admission하지 못한다.

`NrServer::Shutdown`도 submission invalidation을 보장한 뒤 bootstrap component를 생성의 역순으로 종료한다. I/O pipeline과 actor scheduler가 먼저 멈추고 worker가 join된 뒤 actor registry가 남은 session에 `ServerStopping`을 기록하고 최종 lifecycle을 게시한다. 마지막으로 To-World handoff를 닫아 대기 중인 World consumer를 깨운다.

일반 session close에서는 matching completion을 통해 pending context가 정리된 뒤 registry가 actor를 회수한다. 전체 graph shutdown에서는 새 submission과 worker 실행을 먼저 차단하고 join한 상태에서 registry가 남은 actor를 최종 정리한다. 두 경로를 하나의 “completion을 무조건 기다리는 drain”으로 해석하지 않는다.

## Interface와 invariant

- Actor registry만 session actor 객체를 소유한다.
- Actor executor lease가 살아 있는 동안 registry는 해당 entry를 삭제하지 않는다.
- 같은 actor의 mutable session state는 schedule gate가 허용한 drain owner만 변경한다.
- Session은 active recv context와 active send context를 각각 하나만 보유한다.
- Pending context lease가 `OVERLAPPED`, buffer span과 owning payload의 lifetime을 completion까지 연결한다.
- Completion은 Session Key와 context token이 현재 pending operation과 일치할 때만 해당 state를 완료한다.
- Close reason은 처음 기록된 원인을 보존하고 `SessionClosed` publication은 반복하지 않는다.
- Close가 시작되면 새 recv, send와 partial repost를 시작하지 않는다.
- Gateway submit 성공은 actor path admission을 뜻하며 socket completion을 뜻하지 않는다.
- Runtime Session Key는 transport lifetime identity이고 World Entity ID나 gameplay account identity가 아니다.

## Failure와 pressure

| 상황 | 현재 동작 |
| --- | --- |
| malformed 또는 unknown frame | protocol close를 요청하고 해당 frame을 성공한 입력으로 소비하지 않음 |
| receive publication 또는 input admission 실패 | receive pressure close로 전환하고 실패한 frame을 보존 |
| outbound actor queue가 work를 받을 수 없음 | send pressure close로 전환 |
| native recv/send posting 실패 | pending state를 정리하고 transport close로 전환 |
| remote close 또는 failed completion | matching pending context를 해제하고 close reason을 기록 |
| close 뒤 matching completion | cleanup만 수행하고 application work나 repost를 만들지 않음 |
| stale Gateway 또는 closed send channel | public submit을 invalid state로 거절 |
| server stop 중 새 submission | submission gate가 admission 전에 거절 |

Pressure는 동적 무제한 확장으로 숨기지 않는다. Actor는 실패한 admission을 성공한 frame consume이나 전송 완료로 표현하지 않고, session 단위 close 또는 server lifecycle stop으로 소유권을 회수한다.

## 관련 구현과 테스트

| 독자 질문 | 관련 구현 | 관련 테스트 |
| --- | --- | --- |
| Accept 뒤 actor와 World lifecycle은 어떤 순서로 공개되는가? | [`NrIoEventDispatcher.cpp`](../../src/PrivateServer.NetworkRuntime.Internal/NrIoEventDispatcher.cpp), [`NrSessionIoActor.Accept.cpp`](../../src/PrivateServer.NetworkRuntime.Internal/NrSessionIoActor.Accept.cpp) | [`NrToWorldHandoffTests.cpp`](../../src/PrivateServer.NetworkRuntime.InternalTests/NrToWorldHandoffTests.cpp) |
| Completion은 어떻게 단일 actor drain으로 직렬화되는가? | [`NrActorScheduleGate.h`](../../src/PrivateServer.NetworkRuntime.Internal/NrActorScheduleGate.h), [`NrActorExecutor.cpp`](../../src/PrivateServer.NetworkRuntime.Internal/NrActorExecutor.cpp) | [`NrActorScheduleGateTests.cpp`](../../src/PrivateServer.NetworkRuntime.InternalTests/NrActorScheduleGateTests.cpp), [`NrSessionActorSchedulerAdmissionTests.cpp`](../../src/PrivateServer.NetworkRuntime.InternalTests/NrSessionActorSchedulerAdmissionTests.cpp) |
| Recv frame은 언제 consume되고 World로 소유권이 넘어가는가? | [`NrSessionIoActor.Recv.cpp`](../../src/PrivateServer.NetworkRuntime.Internal/NrSessionIoActor.Recv.cpp), [`NrSessionIoOperations.cpp`](../../src/PrivateServer.NetworkRuntime.Internal/NrSessionIoOperations.cpp) | [`NrRecvDispatchFlowTests.cpp`](../../src/PrivateServer.NetworkRuntime.InternalTests/NrRecvDispatchFlowTests.cpp) |
| Partial send와 payload lifetime은 어디서 관리되는가? | [`NrSessionIoActor.Send.cpp`](../../src/PrivateServer.NetworkRuntime.Internal/NrSessionIoActor.Send.cpp), [`NrSendIoContext.cpp`](../../src/PrivateServer.NetworkRuntime.Internal/NrSendIoContext.cpp) | [`NrGatewayTests.cpp`](../../src/PrivateServer.NetworkRuntime.PublicTests/NrGatewayTests.cpp), [`NrSessionSendChannelTests.cpp`](../../src/PrivateServer.NetworkRuntime.PublicTests/NrSessionSendChannelTests.cpp) |
| Actor는 언제 registry에서 제거될 수 있는가? | [`NrSessionActorRegistry.cpp`](../../src/PrivateServer.NetworkRuntime.Internal/NrSessionActorRegistry.cpp), [`NrSessionIoActor.cpp`](../../src/PrivateServer.NetworkRuntime.Internal/NrSessionIoActor.cpp) | actor scheduling과 To-World lifecycle tests |
| Stop은 새 Gateway work와 worker lifetime을 어떻게 닫는가? | [`NrServer.cpp`](../../src/PrivateServer.NetworkRuntime/NrServer.cpp), [`NrBootstrapPlan.cpp`](../../src/PrivateServer.NetworkRuntime.Internal/NrBootstrapPlan.cpp) | [`NrServerSubmissionGateTests.cpp`](../../src/PrivateServer.NetworkRuntime.InternalTests/NrServerSubmissionGateTests.cpp), [`NrServerLifecycleTests.cpp`](../../src/PrivateServer.NetworkRuntime.PublicTests/NrServerLifecycleTests.cpp) |

## 관련 결정

- [ADR 0005: NetworkRuntime DLL Interface Policy](../../docs/adr/0005-network-runtime-dll-interface-policy.md)
- [ADR 0006: NetworkRuntime와 World Server 통합 계약](../../docs/adr/0006-network-runtime-world-integration-contract.md)
- [NetworkRuntime Public DLL 경계](public-runtime-boundary.md)

## 지원 범위와 제약

- 이 문서는 server-facing Runtime Session Actor를 설명한다. Native client runtime은 별도의 completion dispatcher와 client lifecycle을 사용한다.
- Actor는 session별 mutable I/O state를 직렬화한다. 모든 session을 하나의 global actor나 한 worker에 고정한다는 뜻은 아니다.
- Registry close-ready는 일반 session 회수 조건이다. Server graph shutdown은 worker join 뒤 남은 actor를 별도 finalization 경로로 정리한다.
- Queue와 pool은 bounded resource다. 이 문서는 특정 workload의 capacity나 성능을 보장하지 않는다.
