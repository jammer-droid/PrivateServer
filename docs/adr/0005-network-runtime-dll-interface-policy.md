# ADR 0005. NetworkRuntime DLL Interface Policy

Status: Accepted

2026-07-10 보완: send path 완료 전 Internal.lib 도입을 미룬 순서 결정은 ADR 0006의 확정된 Internal.lib/test seam 순서로 대체한다. Public Pimpl, export, ABI policy는 계속 유효하다.

`PrivateServer.NetworkRuntime`은 C++/IOCP 기반 DLL 프로젝트다. 구현이 진행될수록 listener, IOCP pump, dispatcher, session registry, lifecycle object, operation state control 같은 내부 타입이 늘어난다.

이 타입들을 public header에 그대로 노출하면 DLL 소비자가 구현 layout, Win32/Winsock storage, STL container, virtual interface ABI에 결합된다. 이후 `NetworkRuntime` 내부 구조를 바꿀 때 public ABI와 rebuild 범위가 같이 흔들린다.

따라서 새 `Nr` public API는 `docs/design/conventions/dll-boundary.md`의 노출 형태 선택 순서를 따른다.

결정:

- `NrServer` 같은 runtime composition root는 public Pimpl shell로 제공한다.
- public header에는 config/value type, opaque id/value handle, lifetime owner handle, `NrStatus`처럼 외부 caller가 알아야 하는 stable contract만 둔다.
- `NrResult<T>`는 core/internal helper 또는 비-runtime public value helper로 남길 수 있지만, Runtime public function signature는 `NrStatus + out parameter/report` 형태를 기본으로 한다.
- lifecycle object, bootstrap plan, operation state/control/state handler/rules, concrete listener/pump/dispatcher/registry, Win32 handle/storage는 internal header 또는 `Impl` 뒤에 둔다.
- internal virtual interface는 DLL public API로 export하지 않는다. 필요한 경우 `Impl` 뒤에서만 사용한다.
- class 전체 export보다 DLL 밖에서 호출해야 하는 out-of-line public 함수에만 `PSNR_API`를 붙이는 방식을 기본으로 한다.
- 현재 send path 구현 단계에서는 internal static library 또는 별도 internal test target을 새로 도입하지 않는다. 내부 타입을 테스트하기 위해 `PSNR_API`를 붙여 public DLL surface로 승격하지 않는다.
- 현재 Test project는 내부 동작 검증의 안정 seam이라기보다 build/link 가능성 확인에 가깝게 취급한다. 따라서 internal DLL implementation slice의 기본 검증은 Visual Studio build 통과다.
- send path 완료 후에는 내부 구현부 중 직접 테스트가 필요한 코드를 internal static library로 분리하는 구조를 검토한다. DLL project와 Test project는 이 static library를 link하고, 실제 DLL 소비자는 DLL public API만 사용한다.
- DLL export가 필요한 public API가 아니라면, direct internal tests는 static library seam이 생긴 뒤 추가하고 그 전에는 public API 기반 black-box 검증, 우회 가능한 관찰 지점, smoke/e2e 검증을 이슈 범위가 요구할 때만 사용한다.

결과:

- public API는 작고 안정적인 shell 중심으로 유지된다.
- internal implementation은 IOCP/Winsock 구조, bootstrap graph, lifecycle policy를 바꿀 수 있다.
- 테스트는 product-facing caller처럼 public shell/value/handle API를 우선 사용한다. 내부 타입 직접 테스트가 필요해도 현재는 export 범위를 넓히지 않고 build-only 또는 public API 기반 검증으로 제한한다. send path 이후 static library seam을 만들면 Test project는 그 static library를 link해 내부 구현을 검증할 수 있다.
- public API 승격이 필요하면 Pimpl shell, opaque id, lifetime owner handle, internal type 중 하나를 먼저 선택한다.
