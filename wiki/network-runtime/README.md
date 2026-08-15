# NetworkRuntime

> Document status: Reviewed
> Baseline: 1508dacf340e52cb4ec67e7e7a60d05755510553
> Last reviewed: 2026-08-12

## 목적과 범위

NetworkRuntime은 client connection IO, runtime session lifecycle, inbound parsing, outbound framing, recv/send buffer lifetime과 Windows IOCP 실행을 소유한다. World gameplay state와 engine presentation은 소유하지 않는다.

## 현재 제공 범위

| 영역 | 현재 설명 | 관련 구현과 테스트 |
| --- | --- | --- |
| Solution/project 구성 | Runtime DLL, Internal library, adapters, tests와 smoke가 public/internal 경계를 구성 | `PrivateServer.sln`과 project reference |
| Public/internal seam | public SDK header와 internal implementation ownership이 분리됨 | DLL project의 staged header와 PublicTests |
| Accept/Recv/To-World | 실제 gameplay packet이 World ingress와 fixed-step command로 연결됨 | Internal/Public test와 World public loopback |
| Native/C ABI/Managed boundary | Godot client가 C ABI와 Managed adapter를 통해 native client runtime을 사용 | [`Managed Smoke`](../../src/PrivateServer.NetworkRuntime.Managed.Smoke/Program.cs)와 Game Client session seam tests |
| Multi-Channel usage | 각 World Host가 독립 `NrServer` instance와 endpoint를 소유 | Host config와 local fleet integration |

## 주요 경계

```text
PrivateServer.NetworkRuntime.Internal.lib
├─ PrivateServer.NetworkRuntime.dll
│  ├─ PublicTests
│  ├─ Native Smoke
│  └─ CAbi.dll -> Managed -> Managed Smoke
└─ InternalTests
```

- Runtime DLL은 `NrServer`, `NrToWorldEvent`, `NrGateway`, `NrSessionSendChannel`, `NrServerSnapshot`과 client public contract를 제공한다.
- Internal.lib는 IOCP, listener, session actor, parser, buffer, queue/pool과 diagnostics implementation을 소유한다.
- World와 client consumer는 internal header와 Internal.lib를 사용하지 않는다.
- Channel은 World Host가 소유하는 gameplay instance identity이며 NetworkRuntime의 send channel과 다르다. NetworkRuntime은 Host마다 독립 listener와 Runtime Session 집합을 제공한다.

## 우선 scenario

1. Server startup과 listener bootstrap
2. Accept에서 first `WSARecv`까지
3. Recv completion에서 packet parse와 `NrToWorldEvent`까지
4. World submit에서 per-session `WSASend`까지
5. Close, pressure와 shutdown
6. Native/C ABI/Managed client lifecycle

## 상세 문서

- [NetworkRuntime Public DLL 경계](public-runtime-boundary.md): 제품 소비자가 의존할 수 있는 public API, `Internal.lib` 구현 seam과 lifetime 계약
- [Session Actor ownership과 I/O lifetime](session-actor-ownership-and-io-lifetime.md): accept, recv, send, close와 server shutdown에서 mutable state와 pending I/O를 회수하는 경계

## 관련 근거

- `CONTEXT.md`
- `docs/adr/0005-network-runtime-dll-interface-policy.md`
- `docs/adr/0006-network-runtime-world-integration-contract.md`
- `docs/design/network-runtime/`
- `src/PrivateServer.NetworkRuntime.PublicTests/`
- `src/PrivateServer.NetworkRuntime.Smoke/`

## 지원 범위와 제약

- Public DLL 경계는 별도 문서에서 설명하며, generated SDK의 호환성 header와 권장 application contract를 구분한다.
- Accept/Recv/To-World와 World-to-Send의 내부 실행 순서는 이 overview의 범위에 포함하지 않는다.
- 이 overview는 Runtime의 구조와 contract를 설명하며 capacity나 성능 결과를 주장하지 않는다.
