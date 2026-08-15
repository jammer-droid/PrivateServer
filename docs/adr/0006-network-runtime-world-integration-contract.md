# ADR 0006. NetworkRuntime World Integration Contract

Status: Accepted

NetworkRuntime의 World-facing DLL contract는 transport 세부를 숨기면서 submit, event, lifetime 의미를 안정적으로 제공해야 한다. 기존 wire-ready Gateway와 public callback seam은 framing 지식과 thread/lifetime 책임을 World에 전파했고, server보다 오래 사는 channel handle의 안전성도 보장하지 못했다. ADR 0005에서 send path 완료 뒤 검토하기로 한 internal static library seam도 이제 public integration을 구현하기 전에 확정할 수 있는 단계다. 이 ADR은 ADR 0005의 static-library 도입 시점만 대체하고 나머지 DLL boundary policy는 유지한다.

결정:

- `Nr`는 NetworkRuntime public type prefix다. `psnr::runtime` namespace 안에서 `NrRuntime*`처럼 의미를 반복하지 않는다.
- Runtime에서 World로 향하는 owning event type은 `NrToWorldEvent`, server observation value는 `NrServerSnapshot`으로 명명한다.
- `NrToWorldEvent`는 session accepted, session closed, WorldIngress packet received만 전달한다. Packet payload는 event lifetime 동안 event가 소유하며, 초기 event stream에는 pressure notification을 넣지 않는다.
- WorldIngress handoff와 `NrToWorldEvent` drain은 하나의 bounded storage를 공유한다. Runtime Session Key별 관측 순서는 `SessionAccepted -> PacketReceived* -> SessionClosed`이며 lifecycle event는 조용히 drop하지 않는다.
- `INrSessionLifecycleListener`와 `INrRuntimeInputObserver`는 public integration seam에서 제거한다. World와 Smoke는 `NrToWorldEvent`를 drain한다.
- `NrGateway`는 `NrServer`가 발급하는 server-bound send interface다. World는 gameplay/application 의미로 직렬화한 opaque payload bytes와 packet type을 제출하고, NetworkRuntime이 transport header framing과 runtime-owned immutable payload 전환을 수행한다.
- Gateway submit의 `accepted`는 runtime payload, mailbox, actor execution path가 함께 commit됐다는 뜻이며 socket send completion을 뜻하지 않는다. 실패한 single submit은 retry-safe해야 하고 Broadcast partial rejection은 이미 accepted된 recipient를 rollback하지 않는다.
- `NrGateway`와 `NrSessionSendChannel` copy는 server보다 오래 살아도 안전하게 파괴된다. Close authority나 identity를 갖지 않으며, session/server shutdown 이후 새 submit을 `InvalidState`로 거절한다.
- World의 close 요청은 Runtime Session Key와 close reason을 받는 별도 session-control operation으로 제공한다.
- `NrStatus`는 stable error code와 native error code의 작은 return value로 유지한다. 상세 debug/benchmark context는 별도 structured diagnostics contract가 소유한다.
- `NrServerConfig`에는 structured diagnostics를 선택할 최소 configuration seam만 두고, diagnostic record, sink, queue 구현은 public server contract와 분리한다.
- 직접 검증이 필요한 runtime implementation은 `PrivateServer.NetworkRuntime.Internal.lib`로 분리해 DLL과 Tests가 공유한다. World, Smoke, client 같은 실제 DLL consumer는 public headers와 DLL/import library만 사용한다.
- Mailbox commit, schedule state transition, ready queue publish는 하나의 scheduler admission transaction이어야 한다. 이 correctness 작업은 Internal.lib seam 이후, Gateway/To-World integration 전에 별도 issue로 완료한다.

대안으로 World가 wire-ready bytes를 만들게 두면 header codec과 framing 정책이 World public dependency가 된다. Ephemeral packet view와 public callback을 유지하면 World가 buffer lifetime, callback lifetime, thread safety를 다시 조립해야 한다. Send channel에 close 권한을 추가하면 send capability와 administrative control 의미가 섞인다.

결과:

- World-facing public surface는 `NrServer`, `NrToWorldEvent`, `NrGateway`, `NrSessionSendChannel`, `NrServerSnapshot`, session close control로 수렴한다.
- outbound framing responsibility와 handle teardown safety를 NetworkRuntime 안에서 검증해야 한다.
- internal behavior test를 위해 DLL export surface를 넓히지 않으며, scheduler transaction failure를 public submit failure 의미와 함께 검증할 수 있다.
- client interface, diagnostics delivery, pressure recovery policy는 독립 계획으로 관리한다.
