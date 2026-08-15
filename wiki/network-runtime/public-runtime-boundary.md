# NetworkRuntime Public DLL 경계

> Document status: Reviewed
> Baseline: 1508dacf340e52cb4ec67e7e7a60d05755510553
> Last reviewed: 2026-08-12

## 핵심 답

World Server와 native client 같은 제품 소비자는 staged public header와 `PrivateServer.NetworkRuntime` DLL/import library에만 의존한다. IOCP, Winsock, session actor, parser, queue, pool과 diagnostics 구현은 DLL 뒤의 `PrivateServer.NetworkRuntime.Internal.lib`가 소유한다.

`Internal.lib`는 제품 기능을 호출하는 두 번째 API가 아니다. DLL 구현을 직접 검증하기 위한 build/test seam이며, 제품 소비자가 배워야 할 계약은 public DLL 경계 하나다.

## 문제

NetworkRuntime 내부에는 Win32 handle, `OVERLAPPED`, socket, actor mailbox, pooled payload와 동시성 상태처럼 변경 가능성이 높은 구현 세부가 많다. 이를 public class layout이나 header dependency로 노출하면 소비자가 내부 storage와 Windows 구현에 결합되고, 내부 리팩터링이 ABI와 rebuild 범위까지 흔들게 된다.

현재 경계는 외부에 작은 shell, value, view와 capability만 남기고 구현 layout과 실행 graph를 DLL 뒤에 숨긴다. 이 구조는 World와 Client가 transport 구현을 사용하되 IOCP ownership을 함께 떠안지 않도록 한다.

## 책임과 제외 범위

이 문서는 다음을 설명한다.

- 제품 소비자가 사용할 수 있는 public dependency
- server-facing, client-facing public type의 역할
- `NrServer`, `NrGateway`와 `NrClient` operation의 호출 가능 상태와 결과 의미
- view, owning value, event와 capability의 lifetime 차이
- DLL, C ABI, Managed adapter와 `Internal.lib`의 의존·소유권 방향
- public contract를 확인하는 code/test 경계

다음은 이 문서의 범위가 아니다.

- Accept, recv, send completion의 내부 실행 순서
- session actor의 close/drain state transition
- gameplay packet별 semantic payload layout
- capacity와 성능 결과

## Building block과 의존 방향

```text
World Server / Native Client
PublicTests / Native Smoke
             |
             | staged public headers + DLL/import library
             v
PrivateServer.NetworkRuntime.dll
  |-- server-facing: NrServer, NrToWorldEvent, NrGateway,
  |                  NrSessionSendChannel, NrServerSnapshot
  |-- client-facing: NrClient, NrClientEvent, NrClientSnapshot
  |-- shared values: NrStatus, NrSessionKey, NrPacketType,
  |                  NrByteView, config와 reason enum
  |
  | static link; 제품 소비자에게 export하지 않음
  v
PrivateServer.NetworkRuntime.Internal.lib
  |-- IOCP / Winsock / listener / pending IO
  |-- session actor / registry / lifecycle
  |-- framing / parser / payload / queue / pool
  `-- diagnostics implementation

Managed Client
      |
      v
PrivateServer.NetworkRuntime.CAbi.dll
      |
      v
PrivateServer.NetworkRuntime.dll
```

PublicTests, Native Smoke와 C ABI adapter는 generated SDK include root인 `build/include/PrivateServer/NetworkRuntime/`를 사용한다. 이들은 `PrivateServer.NetworkRuntime.Internal` include root를 받지 않는다. 반대로 DLL project는 public wrapper를 구현하기 위해 `Internal.lib`를 link한다.

## Public interface의 역할

### Composition owner

`NrServer`와 `NrClient`는 복사할 수 없고 이동할 수 있는 Pimpl shell이다. Public layout은 `Impl*`만 가지며, lifecycle graph와 Windows storage는 구현 뒤에 남는다.

- `NrServer`: server graph 생성, start/stop/shutdown, To-World event drain, Gateway 발급과 snapshot 관측
- `NrClient`: connect/disconnect/shutdown, packet submit, client event drain과 snapshot 관측

### World-facing event와 capability

`NrToWorldEvent`는 Runtime이 World로 넘기는 move-only owning event다. Event kind에 따라 Runtime Session Key와 다음 값 중 하나를 제공한다.

- `SessionAccepted`: copy-out 가능한 `NrSessionSendChannel`
- `PacketReceived`: `NrPacketType`과 payload view
- `SessionClosed`: `NrSessionEndReason`

`NrGateway`는 `NrServer`가 발급하는 move-only submit capability다. World는 packet type과 semantic payload를 제출하고, NetworkRuntime은 transport header encode, immutable frame ownership과 per-session admission을 담당한다.

`NrSessionSendChannel`은 copyable send-only capability다. Runtime Session identity나 close 권한을 제공하지 않으며, session이 닫힌 뒤에도 handle 자체는 안전하게 검사하고 파괴할 수 있다.

### Observation value와 status

`NrServerSnapshot`과 `NrClientSnapshot`은 capture 이후 원본 owner와 독립적으로 보관할 수 있는 copyable owning value다. `NrStatus`는 operation의 성공·실패와 native error를 전달하며, 개별 occurrence의 시간순 진단이나 복구 정책을 소유하지 않는다.

## Server 호출 계약

### Lifecycle

```text
default / moved-from
        |
        | Create(config, outServer)
        v
     Created --Start()--> Running --RequestStop()--> StopRequested
        |                    |                            |
        `--------------------+----------------------------+
                             | Shutdown()
                             v
                          Shutdown
```

- Default-constructed 또는 moved-from `NrServer`는 invalid holder다. `Create`가 성공해야 public operation을 사용할 수 있다.
- `Create`는 bind endpoint, listener, bounded storage와 additional WorldIngress packet catalog를 검증하고 Runtime graph를 조립한다. `additionalWorldIngressPacketTypes`는 호출 중 borrowed view이며 Runtime이 값을 복사한다.
- `Start`는 `Created`에서만 성공한다. 이미 실행 중이거나 stop/shutdown에 들어간 Server를 다시 시작하지 않는다.
- `RequestStop`은 `Running`에서 admission을 닫고 stop 절차를 시작한다. Stop이 시작된 뒤의 반복 요청은 같은 종료 의도를 유지한다.
- `Shutdown`은 시작 전에도 호출할 수 있으며 graph 종료를 완료한다. 완료 뒤 반복 호출은 같은 terminal state를 유지한다.
- Snapshot의 lifecycle 값은 `Created`, `Running`, `StopRequested`, `Shutdown`을 구분한다. Snapshot은 관측값이며 lifecycle을 변경하는 control handle이 아니다.

### Operation reference

| Operation | 호출 조건 | 성공의 의미 | 주요 실패·종료 분기 |
| --- | --- | --- | --- |
| `NrServer::Create` | invalid output holder와 유효한 `NrServerConfig` | Runtime graph와 bounded resource 구성이 `Created` state로 확정됨 | 잘못된 config/출력은 `InvalidArgument`, 이미 valid한 output은 `InvalidState` |
| `Start` | `Created` | listener, completion과 actor 실행 경계가 `Running`으로 전환됨 | 중복 start 또는 terminal lifecycle은 `InvalidState` |
| `CreateGateway` | `Running` | 현재 Server control에 연결된 move-only Gateway 발급 | start 전, stop 이후, 이미 valid한 output은 `InvalidState` |
| `TryPopToWorldEvent` | valid Server와 output | 다음 owning event 하나를 caller에게 이동 | 현재 event가 없으면 `QueueEmpty`; output은 유효한 event로 바뀌지 않음 |
| `TryPopToWorldEvents` | valid Server와 non-empty output buffer | dequeue된 owning event batch를 caller에게 이동 | event가 없거나 argument가 잘못되면 실패하며 성공하지 않은 output을 새 결과처럼 commit하지 않음 |
| `WaitForToWorldEvents` | valid Server와 result output | `EventsAvailable`, `TimedOut`, `Closed`를 operation 실패와 분리해 반환 | `Closed`는 producer 종료 뒤 남은 event까지 drain된 terminal handoff를 뜻함 |
| `RequestSessionClose` | `Running`과 유효한 Runtime Session Key | 해당 session actor close request의 admission | `SessionClosed`가 World에서 처리됐다는 뜻은 아님 |
| `RequestStop` | `Running` 또는 이미 stop 요청됨 | 새 admission을 닫고 graph stop을 진행 | start 전이나 shutdown 뒤에는 `InvalidState` |
| `Shutdown` | valid Server | Runtime component와 pending work의 terminal 회수 | 반복 호출은 terminal state를 유지 |
| `CaptureSnapshot` | valid Server와 output | capture 시점의 owning observation value 기록 | transactional global snapshot이나 recovery action을 뜻하지 않음 |

`NrStatus::Succeeded()`는 해당 public operation이 정의한 경계까지만 성공했음을 뜻한다. Close request admission, send admission, socket completion과 World-side event consumption은 서로 다른 완료 지점이다.

## World-facing submit 계약

`NrGateway`는 `Running` Server에서만 발급한다. Handle이 `NrServer` object보다 오래 살아도 파괴는 안전하지만, Server가 stop되거나 소유 control이 사라지면 새 submit을 `InvalidState`로 거절한다.

| Operation | 입력과 ownership | 결과 의미 |
| --- | --- | --- |
| `Submit` | 하나의 open `NrSessionSendChannel`, packet type과 호출 중 borrowed semantic payload | 성공하면 immutable Runtime-owned frame과 session actor 경로 admission이 commit됨 |
| `SubmitMany` | caller가 계산한 channel view, packet type과 하나의 shared semantic payload | operation 자체의 성공과 recipient별 `attempted`, `accepted`, `rejected` report를 분리함 |

빈 semantic payload는 유효한 packet body일 수 있다. 반면 non-empty `NrByteView`는 data pointer가 필요하고 transport frame 한계를 넘는 payload는 거절된다. `SubmitMany`는 개별 invalid/closed channel을 recipient rejection으로 집계할 수 있으며, report의 `accepted`는 socket send 완료 수가 아니다.

`NrSessionSendChannel`은 copyable capability이며 Player ID, World Entity Key나 close 권한을 포함하지 않는다. `IsValid`는 handle storage의 유효성, `IsOpen`은 해당 Runtime Session의 send admission 가능성을 나타낸다.

## Client 호출 계약

### Lifecycle과 event ordering

```text
default / disposed
      |
      | Create(config, outClient)
      v
     Idle --Connect(endpoint)--> TransportConnecting
      ^                              |
      |                              +--> TransportConnected event
      |                              |         |
      |                              |         v
      +-- TransportDisconnected <----+---- Disconnect / remote close / failure
      |
      `---------------- Shutdown() ----------------> Shutdown
```

- `NrClient::Create`는 bounded event/payload storage를 검증하고 `Idle` client를 만든다.
- `Connect` 성공은 connection attempt가 native worker에 admission됐다는 뜻이다. 연결 결과는 `TransportConnected` 또는 `TransportConnectionFailed` event로 관측한다.
- `Send`는 `TransportConnected`에서 semantic packet을 제출한다. 성공은 Runtime-owned send work admission이며 peer 수신 완료가 아니다.
- `Disconnect`는 local disconnect를 요청하고 terminal transport 결과는 `TransportDisconnected` event로 전달한다. Remote close와 transport/protocol failure도 같은 event kind와 reason/status로 구분한다.
- `TryPopEvent`는 caller-facing queue가 비어 있으면 `QueueEmpty`를 반환한다. Popped event는 move-only owner이며 payload view는 event lifetime에 묶인다.
- `Shutdown`은 terminal cleanup이며 반복 호출할 수 있다. Shutdown 뒤 lifecycle snapshot은 계속 capture할 수 있지만 connect, disconnect, send와 event pop은 새 operation을 받지 않는다.

| `NrClientEventKind` | 함께 읽는 값 | 의미 |
| --- | --- | --- |
| `TransportConnected` | kind | Connect attempt가 실제 transport connection으로 완료됨 |
| `TransportConnectionFailed` | transport status | Connect admission 뒤 native connection이 실패함 |
| `PacketReceived` | packet type, payload view | 완전한 transport frame의 semantic payload를 owning event가 보유함 |
| `TransportDisconnected` | transport status, disconnect reason | local request, remote close, pressure, transport 또는 protocol 원인으로 connection lifetime이 끝남 |

Event kind에 맞지 않는 accessor는 성공하지 않으며 caller의 기존 output을 새 값처럼 덮어쓰지 않는다.

## C ABI와 Managed ownership

Client adapter chain은 native lifetime을 언어 경계 밖으로 그대로 노출하지 않는다.

```text
NrClient / NrClientEvent
       |
       | opaque pointer + POD value
       v
psnr_client / psnr_client_event
       |
       | SafeHandle + value mapping + payload copy
       v
NetworkRuntimeClient / NetworkRuntimeEvent
```

### C ABI

- `psnr_client_create`는 opaque `psnr_client*`를 만들고 `psnr_client_destroy`가 owner를 회수한다.
- Connect, disconnect, shutdown, send와 snapshot operation은 C++ `NrStatus`와 lifecycle 의미를 POD `psnr_status`와 enum 값으로 투영한다.
- `psnr_client_try_pop_event`가 반환한 `psnr_client_event*`는 caller가 `psnr_client_event_destroy`로 회수한다.
- Event payload accessor의 `psnr_byte_view`는 event handle이 살아 있는 동안만 유효하다. C caller가 더 오래 보관하려면 그 안에서 bytes를 복사해야 한다.
- C ABI는 C++ class layout, STL storage, exception과 Win32 handle을 노출하지 않는다.

### Managed adapter

- `NetworkRuntimeClient`는 `SafeClientHandle`을 소유하고 `Dispose`에서 native client destroy를 호출한다. Dispose 뒤 public operation은 `ObjectDisposedException`으로 caller misuse를 구분한다.
- Constructor는 native create 실패를 `NetworkRuntimeException`으로 바꾸고, lifecycle operation은 `NetworkRuntimeStatus`를 반환한다.
- `TryPopEvent`는 native `QueueEmpty`를 `false`로 투영한다. 다른 accessor 실패는 exception으로 올린다.
- Native event는 임시 `SafeClientEventHandle`이 소유한다. Managed adapter는 kind별 value와 payload bytes를 `NetworkRuntimeEvent`로 복사한 뒤 native event handle을 즉시 회수한다.
- 따라서 `NetworkRuntimeEvent.Payload`는 native event와 `NetworkRuntimeClient` shutdown 뒤에도 Managed-owned value로 읽을 수 있다.

Game Client는 이 Managed contract 위에 transport generation과 gameplay session state를 추가한다. Godot main-thread 적용 순서는 [Main thread session과 presentation lifecycle](../game-client/main-thread-session-and-presentation-lifecycle.md)이 소유한다.

## Interface invariant

| 경계 | Caller가 의존할 수 있는 invariant |
| --- | --- |
| Public header | STL container, Win32 concrete storage와 internal virtual interface를 public object layout에 노출하지 않는다. |
| Runtime operation | Public runtime signature는 `NrStatus`와 out value/report를 사용한다. 실패한 operation은 성공하지 않은 결과를 완료된 동작처럼 표현하지 않는다. |
| `NrByteView` input | 호출 중에만 유효한 borrowed view다. `size > 0`이면 `data`가 필요하다. Submit이 성공하면 Runtime-owned frame과 actor 실행 경로 admission이 commit되어 caller가 원본 bytes를 파괴할 수 있다. |
| Event payload view | `NrToWorldEvent` 또는 `NrClientEvent`가 payload owner다. 반환된 view는 해당 event가 이동·파괴되기 전까지만 유효하다. |
| `NrGateway` submit | 성공은 Runtime-owned work admission을 뜻한다. Socket completion이나 client 수신 완료를 뜻하지 않는다. |
| `NrClient::Connect` | 성공은 connect attempt admission이다. 실제 성공·실패는 caller-facing event로 완료된다. |
| `NrClient::TryPopEvent` | event가 없다는 `QueueEmpty`와 invalid lifecycle/argument failure를 구분한다. |
| Snapshot | Capture 시점의 owning observation value다. 여러 atomic source 전체의 transactional snapshot은 약속하지 않는다. |
| Internal implementation | IOCP, actor, parser, queue와 pool 구현은 `Internal.lib`에 남고 제품 소비자에게 export하지 않는다. |

## Ownership과 lifetime

```text
caller semantic bytes
  -- borrowed during Submit()-->
NetworkRuntime-owned immutable wire frame
  --> session actor mailbox
  --> pending send / partial repost / completion

Runtime-owned received payload
  --move-->
NrToWorldEvent owner
  --borrowed view while event lives-->
World decode
```

- `NrServer`와 `NrClient`는 각 runtime graph의 outer lifetime owner다.
- `NrGateway`와 `NrSessionSendChannel`은 owner보다 오래 보관될 수 있지만, owner가 stop/shutdown되거나 session이 닫히면 새 operation을 거절한다.
- `NrToWorldEvent`와 `NrClientEvent`는 payload lifetime을 소유한다. Raw payload view를 event보다 오래 사는 queue나 domain object에 저장하면 안 된다.
- World가 payload를 장기 보관해야 한다면 event가 살아 있는 동안 typed command 또는 World-owned storage로 변환해야 한다.
- C ABI event view는 opaque event handle에 묶이고, Managed adapter는 이를 owning value로 복사한 다음 native event를 회수한다.

## Failure, pressure와 shutdown

Public boundary는 내부 failure를 구현 type 노출로 전달하지 않는다.

- 잘못된 argument, lifecycle state, closed/stale capability와 capacity failure는 `NrStatus`로 분기한다.
- `NrGatewaySendReport`는 multi-recipient submit의 recipient별 admission 결과를 전달하지만 socket completion을 보고하지 않는다.
- `NrServerSnapshot`은 lifecycle과 Runtime pressure 상태를 immutable observation value로 투영한다.
- Stop/shutdown은 새 submission admission을 닫는다. 이미 Runtime actor path에 commit된 work와 pending IO의 drain은 내부 lifecycle이 소유한다.

이 분리는 World나 Client가 retry, disconnect 또는 gameplay policy를 선택할 수 있게 하면서도 actor, pool과 IO context를 public API로 끌어올리지 않는다.

## 관련 구현과 테스트

| 독자 질문 | 관련 구현 | 관련 테스트 |
| --- | --- | --- |
| 제품 소비자가 의존하는 build 경계는 무엇인가? | [`PrivateServer.NetworkRuntime.vcxproj`](../../src/PrivateServer.NetworkRuntime/PrivateServer.NetworkRuntime.vcxproj) | [`PrivateServer.NetworkRuntime.PublicTests.vcxproj`](../../src/PrivateServer.NetworkRuntime.PublicTests/PrivateServer.NetworkRuntime.PublicTests.vcxproj) |
| `NrServer`의 shell과 runtime graph 수명은 어떻게 분리되는가? | [`NrServer.h`](../../src/PrivateServer.NetworkRuntime/NrServer.h) | [`NrServerLifecycleTests.cpp`](../../src/PrivateServer.NetworkRuntime.PublicTests/NrServerLifecycleTests.cpp) |
| To-World event와 payload view의 owner는 누구인가? | [`NrToWorldEvent.h`](../../src/PrivateServer.NetworkRuntime/NrToWorldEvent.h) | [`NrToWorldEventTests.cpp`](../../src/PrivateServer.NetworkRuntime.PublicTests/NrToWorldEventTests.cpp) |
| Gateway와 send channel capability는 언제 무효화되는가? | [`NrGateway.h`](../../src/PrivateServer.NetworkRuntime/NrGateway.h), [`NrSessionSendChannel.h`](../../src/PrivateServer.NetworkRuntime/NrSessionSendChannel.h) | [`NrGatewayTests.cpp`](../../src/PrivateServer.NetworkRuntime.PublicTests/NrGatewayTests.cpp) |
| Server snapshot은 capture 뒤 어떤 수명을 가지는가? | [`NrServerSnapshot.h`](../../src/PrivateServer.NetworkRuntime/NrServerSnapshot.h) | [`NrServerSnapshotTests.cpp`](../../src/PrivateServer.NetworkRuntime.PublicTests/NrServerSnapshotTests.cpp) |
| C ABI adapter는 어느 Runtime 경계를 소비하는가? | [`psnr_cabi.h`](../../src/PrivateServer.NetworkRuntime.CAbi/psnr_cabi.h) | [`PrivateServer.NetworkRuntime.CAbi.vcxproj`](../../src/PrivateServer.NetworkRuntime.CAbi/PrivateServer.NetworkRuntime.CAbi.vcxproj) |
| Managed adapter는 native event와 payload를 어떻게 회수하는가? | [`NetworkRuntimeClient.cs`](../../src/PrivateServer.NetworkRuntime.Managed/NetworkRuntimeClient.cs), [`SafeHandles.cs`](../../src/PrivateServer.NetworkRuntime.Managed/SafeHandles.cs) | [`Program.cs`](../../src/PrivateServer.NetworkRuntime.Managed.Smoke/Program.cs) |

## 지원 범위와 제약

Generated SDK에는 public runtime header 외에 호환성 header인 `NrDispatchLane.h`, `NrPacketHeader.h`, `NrResult.h`, `NrTypeTraits.h`도 함께 배포된다.

- Native Smoke는 test protocol frame을 만들 때 `NrPacketHeader.h`를 사용한다.
- `NrSessionSendChannel.h`는 compile-time concept assertion을 위해 `NrTypeTraits.h`를 포함한다.
- Application consumer의 권장 계약은 `NrServer`, `NrClient`, event, capability, snapshot과 shared public value로 구성된 runtime API다. 호환성 header는 새 application dependency를 정의하지 않는다.

PublicTests와 C ABI adapter는 staged include root와 DLL/import library 경계를 유지한다. Native Smoke의 test protocol helper까지 포함한 SDK package 범위와 제품 application contract의 범위는 동일하지 않다.

C ABI project가 public NetworkRuntime DLL을 소비하는 dependency는 source와 project reference에서 확인할 수 있다. Managed Smoke는 adapter를 통과한 packet round trip과 payload ownership을 확인하는 실행 경계이며, 별도의 Managed unit-test project를 뜻하지 않는다.

## 관련 결정

- [ADR 0005: NetworkRuntime DLL Interface Policy](../../docs/adr/0005-network-runtime-dll-interface-policy.md)
- [ADR 0006: NetworkRuntime와 World Server 통합 계약](../../docs/adr/0006-network-runtime-world-integration-contract.md)
- [DLL 경계 규칙](../../docs/design/conventions/dll-boundary.md)

## 설계 의미

이 경계는 Public caller가 알아야 할 lifetime과 failure 의미를 명시하면서 Windows IO, actor scheduling과 memory ownership 구현은 DLL 내부에서 독립적으로 변경하고 검증할 수 있게 한다.

하나의 public contract와 별도의 internal test seam을 두면 다음 trade-off를 분리할 수 있다.

- ABI 안정성과 internal testability를 같은 export surface로 해결하지 않은 이유
- borrowed input, owning event와 lifetime-safe capability를 구분한 이유
- submit admission과 socket completion을 서로 다른 성공 의미로 둔 이유
- public observation을 snapshot/status로 제공하고 내부 actor나 diagnostic sink를 노출하지 않은 이유
