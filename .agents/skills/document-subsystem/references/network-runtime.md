# NetworkRuntime Documentation Reference

Use this file only when the selected subsystem is `network-runtime`. It routes investigation; it does not override the current solution, code, tests, ADRs, or evidence.

## Baseline map

Start with `PrivateServer.sln` and verify the current projects instead of copying this list blindly.

```text
PrivateServer.NetworkRuntime.Internal.lib
├─ PrivateServer.NetworkRuntime.dll
│  ├─ PrivateServer.NetworkRuntime.PublicTests
│  ├─ PrivateServer.NetworkRuntime.Smoke
│  └─ PrivateServer.NetworkRuntime.CAbi.dll
│     └─ PrivateServer.NetworkRuntime.Managed
│        └─ PrivateServer.NetworkRuntime.Managed.Smoke
└─ PrivateServer.NetworkRuntime.InternalTests
```

Read these policy anchors before describing the boundary:

- `CONTEXT.md`
- `docs/adr/0005-network-runtime-dll-interface-policy.md`
- `docs/adr/0006-network-runtime-world-integration-contract.md`
- `docs/design/conventions/dll-boundary.md`
- `wiki/network-runtime/`
- `src/PrivateServer.NetworkRuntime/`
- `src/PrivateServer.NetworkRuntime.Internal/`

## Candidate areas

When the user omits the area, offer only candidates supported by the current tree.

| Area | Reader question | Primary output candidate |
| --- | --- | --- |
| `public-dll-boundary` | What may a World or Client consumer depend on? | `public-runtime-boundary.md` |
| `startup` | How is the server graph created and started? | `server-startup.md` |
| `accept-recv-to-world` | How does socket input become a World event? | `session-actor-ownership-and-io-lifetime.md` |
| `world-to-send` | How does World output become per-session send IO? | `session-actor-ownership-and-io-lifetime.md` |
| `close-drain-shutdown` | How are new work, pending IO, and owners drained? | `session-actor-ownership-and-io-lifetime.md` |
| `client-lifecycle` | How do Native, C ABI, and Managed client lifecycles align? | `client-lifecycle.md` |

Do not choose an area on behalf of the user when more than one materially different candidate fits.

## Public DLL boundary route

Inspect:

- `src/PrivateServer.NetworkRuntime/PrivateServer.NetworkRuntime.vcxproj`
- `src/PrivateServer.NetworkRuntime.Internal/PrivateServer.NetworkRuntime.Internal.vcxproj`
- `src/PrivateServer.NetworkRuntime/NrServer.h`
- `src/PrivateServer.NetworkRuntime/NrGateway.h`
- `src/PrivateServer.NetworkRuntime/NrSessionSendChannel.h`
- `src/PrivateServer.NetworkRuntime/NrToWorldEvent.h`
- `src/PrivateServer.NetworkRuntime/NrServerSnapshot.h`
- `src/PrivateServer.NetworkRuntime/NrClient.h`
- `src/PrivateServer.NetworkRuntime.CAbi/psnr_cabi.h`
- `src/PrivateServer.NetworkRuntime.Managed/`

Then inspect matching consumer evidence:

- `src/PrivateServer.NetworkRuntime.PublicTests/`
- `src/PrivateServer.NetworkRuntime.Smoke/main.cpp`
- `src/PrivateServer.NetworkRuntime.Managed.Smoke/Program.cs`

Answer:

- which headers are staged as public SDK headers;
- which project owns IOCP, session, parser, queue, pool, and diagnostics implementation;
- which values are owning values, cheap handles, views, or opaque implementation boundaries;
- which lifecycle and failure behavior callers must know;
- whether any migration-only public dependencies remain in the project configuration.

## Runtime scenario routes

### Startup

- `src/PrivateServer.NetworkRuntime/NrServer.cpp`
- `src/PrivateServer.NetworkRuntime.Internal/NrServerGraphBuilder.*`
- `src/PrivateServer.NetworkRuntime.Internal/NrServerComponentGraph.*`
- `src/PrivateServer.NetworkRuntime.Internal/NrServerLifecycleOrder.*`

### Accept and recv to World

- `src/PrivateServer.NetworkRuntime.Internal/NrListener.*`
- `src/PrivateServer.NetworkRuntime.Internal/NrSessionIoActor.Accept.cpp`
- `src/PrivateServer.NetworkRuntime.Internal/NrSessionIoActor.Recv.cpp`
- `src/PrivateServer.NetworkRuntime.Internal/NrSessionIoOperations.*`
- `src/PrivateServer.NetworkRuntime.Internal/NrPacketParser.*`
- `src/PrivateServer.NetworkRuntime.Internal/NrToWorldHandoff.*`
- `src/PrivateServer.NetworkRuntime/NrToWorldEvent.*`

### World to send

- `src/PrivateServer.NetworkRuntime/NrGateway.*`
- `src/PrivateServer.NetworkRuntime/NrSessionSendChannel.*`
- `src/PrivateServer.NetworkRuntime.Internal/NrSessionSendChannelControl.*`
- `src/PrivateServer.NetworkRuntime.Internal/NrSessionIoActor.Send.cpp`
- `src/PrivateServer.NetworkRuntime.Internal/NrSendIoContext.*`

### Close, drain, and shutdown

- `src/PrivateServer.NetworkRuntime/NrServer.cpp`
- `src/PrivateServer.NetworkRuntime.Internal/NrServerLifecycleOrder.*`
- `src/PrivateServer.NetworkRuntime.Internal/NrSessionIoActor.Drain.cpp`
- `src/PrivateServer.NetworkRuntime.Internal/NrSessionActorRegistry.*`
- `src/PrivateServer.NetworkRuntime.Internal/NrServerSubmissionGate.*`

### Client lifecycle

- `src/PrivateServer.NetworkRuntime/NrClient.*`
- `src/PrivateServer.NetworkRuntime.Internal/NrClientConnection.*`
- `src/PrivateServer.NetworkRuntime.Internal/NrClientLifecycleStateMachine.*`
- `src/PrivateServer.NetworkRuntime.CAbi/`
- `src/PrivateServer.NetworkRuntime.Managed/`

## Source-use rules

- Use PublicTests privately to inspect public caller contracts.
- Use InternalTests privately to inspect internal invariants and state-machine behavior.
- Use Native and Managed smoke code privately to inspect integration paths.
- Treat a test file name as source navigation, not proof that the current baseline passed.
- Keep benchmark artifacts, run results, pass rates, measurements and evaluation rubrics outside the public whitelist.
- Public documents may link to implementation and test source, but must not reproduce internal execution results or score the strength of evidence.

## Writing focus

Prefer these questions over class inventories:

- What complexity does the DLL hide from its consumers?
- Which interface is the stable seam between Runtime, World, and Client?
- Who owns mutable session state and pending IO?
- When does ownership move across a queue, event, handle, or ABI boundary?
- What can a consumer observe when admission, pressure, close, or shutdown rejects work?
- Which implementation and test source lets a reader continue investigating the claim?
