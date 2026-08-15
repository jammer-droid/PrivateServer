# IO hot path 규칙

## 목적

NetworkRuntime의 recv/send hot path에서 copy, lock, lifetime 최적화 기준을 고정한다.

이 문서는 무조건 zero-copy 또는 lock-free를 요구하지 않는다. 목표는 안전한 ownership boundary를 유지하면서 application-level copy와 hot-path lock이 어디에서 발생하는지 명시하고, 후속 최적화가 같은 기준으로 판단되게 하는 것이다.

## 기본 원칙

- pending IO ownership, completion identity, buffer lifetime invariant가 최우선이다.
- copy/lock 제거가 ownership 또는 completion lifetime을 흐리면 적용하지 않는다.
- 추가 copy나 새 lock을 hot path에 넣어야 하면 이유, 경계, 예상 비용을 문서나 코드 근처에 남긴다.
- 최적화는 계측 없이 바로 적용하지 않는다.

## Copy boundary

Application-level copy 1회는 성능 실패가 아니라 ownership boundary 전환 비용으로 본다.

기본 규칙:

- user process 내부에서 caller-owned bytes 또는 recv-buffer-owned bytes를 runtime-owned payload로 승격하는 copy는 경계당 최대 1회를 기본값으로 둔다.
- recv는 complete frame을 actor 밖 ingress/world boundary로 넘기기 전에 runtime-owned payload로 1회 copy한다.
- WorldIngress packet은 그 owned payload를 To-World handoff storage에 넣고 drain 시 `NrToWorldEvent`로 move-convert한다. 중간 `NrInput` queue와 public event queue를 직렬로 두거나 payload bytes를 다시 copy하지 않는다.
- send는 Gateway submit boundary에서 caller-owned semantic payload와 packet type을 runtime-owned immutable wire frame으로 1회 copy/encode한다.
- actor queue, IO context, completion, partial repost 경로에서는 payload byte copy를 추가하지 않는다.

현재 구조:

```text
Recv:
kernel -> user recv buffer        // OS/IO boundary
recv buffer -> runtime payload    // application-level copy 1회
ingress/world 전달                // 추가 payload byte copy 없음

Send:
caller packetType + semantic payload -> runtime framed PayloadRef // application-level copy/encode 1회
framed PayloadRef -> WSABUF view                          // 추가 payload byte copy 없음
partial repost/full completion                            // 추가 payload byte copy 없음
```

예외:

- `RecvBuffer::Compact()` 같은 partial frame 보존 작업은 payload 변환 copy가 아니라 recv buffer 내부 정렬 비용이다.
- protocol transform, compression, encryption처럼 bytes 자체가 바뀌는 단계가 추가되면 별도 transform boundary로 문서화한다.

## Raw pointer boundary

Raw `WSABUF` pointer를 actor/runtime ownership boundary 밖으로 노출하지 않는다.

금지:

- `WSABUF.buf` 또는 recv buffer `span`을 World/ingress consumer가 장기 보관하는 형태.
- caller-owned stack/vector/string buffer를 IOCP completion까지 보장되는 owner handle 없이 `WSABUF`에 직접 연결하는 형태.
- recv buffer 내부 frame view를 payload처럼 넘기면서 buffer reuse/compact를 계속 허용하는 형태.

허용:

- `WSABUF`는 active pending IO context 내부에서만 payload 또는 recv buffer storage를 가리킨다.
- actor 밖으로 전달되는 bytes는 runtime-owned payload/ref/lease처럼 lifetime이 타입으로 표현된 handle을 통해 전달한다.

## Lock boundary

Actor-owned session state는 actor lane에서 lock 없이 변경한다.

허용되는 현재 lock boundary:

- shared memory pool acquire/release.
- payload block/control block release.
- overlapped context acquire/release.
- producer가 actor mailbox에 enqueue할 때 필요한 queue synchronization.

현재 recv/send context release 지점:

```text
RecvCompleted
-> session.ClearPendingRecv()
-> NrRecvIoContextLease::Reset()
-> overlapped context block release
-> memory pool lock 가능

SendCompleted full/failure
-> session.ClearPendingSend()
-> NrSendIoContextLease::Reset()
-> overlapped context block release
-> memory pool lock 가능
```

따라서 recv/send context 최적화는 acquire만이 아니라 release lock도 함께 줄이는 방향으로 판단한다.

Hot path에 새 lock을 추가할 때는 다음을 확인한다.

- actor-owned state를 보호하려는 lock인지, shared resource boundary lock인지 구분한다.
- lock scope가 native IO call, parser loop, completion cleanup 전체를 감싸지 않는지 확인한다.
- contention evidence 없이 correctness와 무관한 lock을 추가하지 않는다.

## Send partial repost

Partial send repost는 payload byte copy를 만들지 않는다.

기본 규칙:

- active `NrSendIoContext`가 같은 `NrPayloadRef`를 계속 소유한다.
- completion transferred bytes를 누적해 sent offset을 갱신한다.
- `WSABUF.buf`와 `WSABUF.len`만 remaining range로 갱신한다.
- full completion에서만 active send context와 payload ref ownership을 release한다.

따라서 partial repost는 새 payload block을 만들지 않고, 가능하면 새 overlapped context도 acquire하지 않는다.

## Recv zero-copy 후보

Recv zero-copy는 recv buffer segment loan 계약이 생기기 전까지 기본 금지다.

이유:

- recv buffer는 다음 `WSARecv`에서 재사용된다.
- partial frame 보존을 위해 `Compact()`가 bytes 위치를 바꿀 수 있다.
- ingress/world consumer lifetime이 recv buffer lifetime보다 길 수 있다.

Recv zero-copy를 도입하려면 최소한 다음 계약이 필요하다.

- frame segment를 lease/ref-count로 소유할 수 있어야 한다.
- consumer가 release하기 전까지 해당 segment는 재사용되거나 compact로 이동되면 안 된다.
- lease된 segment 때문에 recv capacity가 묶일 때 backpressure 정책이 있어야 한다.
- 여러 frame이 같은 recv buffer block에 있을 때 segment별 lifetime이 분리되어야 한다.

단순 `span`/raw pointer를 runtime payload 대신 넘기는 것은 허용하지 않는다.

## Send zero-copy 후보

Send zero-copy는 caller/runtime producer가 이미 send-safe immutable buffer owner handle을 제공할 때만 검토한다.

필수 조건:

- buffer는 immutable이어야 한다.
- memory location은 IOCP completion 전까지 이동하거나 해제되면 안 된다.
- ownership은 ref-counted/pinned/lease handle처럼 타입으로 표현되어야 한다.
- broadcast/fanout은 recipient별 copy가 아니라 같은 buffer handle 공유로 처리할 수 있어야 한다.
- completion/full cleanup에서 release 시점이 명확해야 한다.

`NrGateway::Send/Broadcast`는 caller buffer lifetime을 신뢰하지 않는다. Packet type과 semantic payload를 받아 submit boundary에서 header를 encode하고 runtime-owned framed payload ref로 1회 전환한다.

## 최적화 후보

다음 후보는 계측 이후 별도 이슈로 판단한다.

- per-worker batch cache.
- worker-local memory pool과 session shard affinity.
- deferred cross-thread release.
- fixed-block pool lock-free free list.
- recv buffer segment loan 기반 zero-copy.
- recv overlapped context 재사용.
- send-safe external payload ref submit API.

### Recv overlapped context 재사용 후보

현재 recv path는 각 `WSARecv` post마다 `NrRecvIoContextLease`를 acquire하고, `RecvCompleted` 처리에서 pending recv를 clear/release한다. 따라서 TCP partial recv나 작은 packet이 많아 recv IO attempt가 자주 발생하면 overlapped context pool acquire/release lock이 반복될 수 있다.

후속 최적화 후보는 session당 one-outstanding recv invariant를 이용해 recv context를 재사용하는 것이다.

가능한 방향:

- session이 recv context를 장기 보유하고, 다음 `WSARecv` 전에 `OVERLAPPED` native fields만 reset한다.
- session 내부 fixed recv context storage를 둔다.
- worker-local recv context cache를 둬 shared pool lock 빈도를 줄인다.

주의:

- pending recv identity와 `contextToken` 검증 의미가 유지되어야 한다.
- close/drain 중 pending IO가 있는 context는 completion 전까지 release되면 안 된다.
- recv buffer writable range는 매 post마다 갱신되어야 한다.
- recv payload copy boundary와는 별개다. 이 후보는 payload byte copy 제거가 아니라 overlapped context acquire/release lock 감소를 목표로 한다.

우선 계측할 지표:

- pool role별 acquire/release count.
- pool role별 lock wait 또는 contention count.
- payload size 분포.
- recv `Compact()` 빈도.
- pending send queue backlog.
- partial send completion 빈도.
- fanout payload의 마지막 release thread 분포.
