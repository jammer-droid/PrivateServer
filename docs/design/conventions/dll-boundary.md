# DLL 경계 규칙

## 목적

`PrivateServer.NetworkRuntime` DLL의 public header가 구현 세부와 ABI를 과하게 노출하지 않도록 한다.

NetworkRuntime은 World Server, smoke tool, test executable 같은 외부 프로세스에서 로드될 수 있다. 따라서 DLL 밖에서 필요한 interface만 노출하고, 구현 storage와 platform 세부는 가능한 한 DLL 내부에 둔다.

## 기본 원칙

- class 전체에 `PSNR_API`를 붙이지 않는다.
- DLL 밖에서 호출해야 하는 out-of-line public 함수에만 `PSNR_API`를 붙인다.
- DLL public header에는 안정적인 small value type, enum, opaque handle, pointer-sized owner만 노출한다.
- STL container, custom deleter, allocator, mutex, Win32 concrete storage는 public class layout에 노출하지 않는다.
- 구현 layout이 바뀔 가능성이 있는 type은 public module, opaque id, lifetime owner handle, internal type 중 하나로 의도를 먼저 고른다.
- public header는 구현 조립 방식, lifecycle hook, operation state/control, state handler, concrete listener/pump/dispatcher/registry를 설명하거나 노출하는 장소가 아니다.

## Target public surface

장기 목표는 DLL 밖 caller가 `psnr::runtime` namespace의 public API만 의존하는 것이다.

```text
World Server / smoke tool / external process
  -> psnr::runtime public shell, config, id, binding, snapshot
      -> psnr::core primitive
      -> Win32 / IOCP
```

`psnr::core`는 memory pool, recv buffer, packet parser, dispatch table, overlapped context, queue 같은 구현 primitive를 담는 내부 구현 축으로 수렴시킨다.

현재 일부 core header는 public처럼 노출되어 있고 test project가 직접 include한다. 이 상태는 기존 구현의 현실로 인정하되, 신규 Runtime 작업에는 확대하지 않는다.

현재 Runtime 구현 기준:

- 신규 external-facing API는 `psnr::runtime` namespace에 둔다.
- external-facing Runtime header는 core primitive concrete type을 직접 노출하지 않는다.
- Runtime public header는 core를 조립하는 방식, lifecycle object, bootstrap plan, operation state, IO event dispatcher, pending IO context를 숨긴다.
- Runtime public function signature에는 `NrResult<T>`를 노출하지 않는다.
- Runtime public failure는 `NrStatus`로 반환하고, 생성 결과는 out object/opaque id/handle로 전달한다.
- Core primitive 테스트는 유지할 수 있지만, product-facing API 검증으로 취급하지 않는다.
- Runtime behavior 테스트는 외부 caller 관점으로 public Runtime header만 include한다.
- send path가 완료됐으므로 ADR 0006과 server public-interface 계획에 따라 직접 검증이 필요한 구현을 Internal.lib로 분리한다.
- DLL project와 Test project는 Internal.lib를 공유하고, 실제 DLL 소비자는 계속 public headers와 DLL/import library만 사용한다.
- 내부 Runtime 타입에 대한 직접 링크 테스트가 필요해도 `PSNR_API`를 붙여 public API로 승격하지 않는다.
- Test project는 public DLL behavior test와 Internal.lib direct test를 구분한다.
- Internal.lib 분리 전까지 남은 slice는 기존 기준대로 build/link viability와 public API 기반 smoke/e2e를 사용한다.

이 기준을 지키면 core/internal implementation을 Internal.lib test seam으로 옮겨도 외부 프로세스의 public dependency를 넓히지 않을 수 있다.

## 노출 형태 선택 순서

새 NetworkRuntime type을 만들 때는 먼저 DLL 밖 caller가 무엇을 알아야 하는지 결정한다.

```text
외부 caller가 이 module의 행동을 직접 호출해야 하는가?
  yes -> public module + Pimpl

외부 caller가 identity만 보관하고 다시 넘기면 되는가?
  yes -> opaque id/value handle

외부 caller가 pending resource의 lifetime만 안전하게 들고 있어야 하는가?
  yes -> lifetime owner handle

외부 caller가 전혀 알 필요 없는가?
  yes -> internal type
```

이 순서에서 뒤로 갈수록 public header가 알아야 하는 정보가 줄어든다. IOCP, Winsock, memory pool, queue node처럼 구현 세부에 가까운 type은 가능한 한 뒤쪽 형태를 선택한다.

## 피해야 할 형태

```cpp
class PSNR_API RuntimeObject
{
public:
    RuntimeObject();
    ~RuntimeObject();

private:
    OVERLAPPED overlapped_;
    WSABUF buffer_;
    std::vector<std::byte> storage_;
};
```

이 형태는 private member까지 DLL 소비자가 컴파일 시 알아야 하는 객체 layout이 된다. MSVC C4251류 경고, STL ABI coupling, Win32 include 전파, rebuild 범위 증가를 만든다.

## 권장 형태: Pimpl

외부 caller가 module의 행동을 직접 호출해야 하지만 구현 layout은 숨기고 싶을 때 사용한다.

예:

- `NetworkRuntime`
- `MemoryPoolManager`
- `PacketCodec`
- `SessionRegistry`

```cpp
class RuntimeObject
{
public:
    PSNR_API RuntimeObject();
    PSNR_API ~RuntimeObject() noexcept;

    RuntimeObject(const RuntimeObject&) = delete;
    RuntimeObject& operator=(const RuntimeObject&) = delete;

    PSNR_API RuntimeObject(RuntimeObject&& other) noexcept;
    PSNR_API RuntimeObject& operator=(RuntimeObject&& other) noexcept;

    [[nodiscard]] PSNR_API NrStatus Reset() noexcept;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};
```

`Impl` 정의는 `.cpp` 또는 internal header에 둔다. `Impl` 내부에서는 STL, Win32 type, pool block, allocator, synchronization primitive를 자유롭게 사용할 수 있다.

`Impl` 내부에서는 module-private interface나 virtual dispatch를 사용할 수 있다. public header는 stable shell만 노출하고, registry, channel, handler adapter, STL container 같은 구현 세부는 `Impl` 또는 internal header에 둔다.

```text
public module
  -> Pimpl shell
  -> Impl owns internal interfaces and concrete implementations
```

외부 caller가 직접 구현해야 하는 plugin callback/interface가 아니라면 public virtual interface를 만들지 않는다. public virtual interface는 vtable layout, destructor ownership, argument type ABI, function ordering을 public contract로 만들기 때문이다.

## NetworkRuntime server shell 적용 규칙

`NrServer` 같은 runtime composition root는 public Pimpl shell로 시작한다.

Public header가 노출할 수 있는 것은 외부 caller가 서버를 만들고 제어하는 데 필요한 계약뿐이다.

허용:

- `NrServer`
- `NrServerBuilder` 또는 equivalent factory shell
- `NrServerConfig`
- `NrStatus`
- opaque session/connection id
- ingress/outbound binding value
- immutable stats/snapshot value

금지:

- `INrServerLifecycleComponent`
- bootstrap plan concrete storage
- `NrBootstrapContext`, `NrStopContext`가 internal orchestration 전용이면 public 노출 금지
- server operation state/control/state handler/rules
- listener, completion pump, IO event dispatcher, session registry concrete type
- Win32 handle, `OVERLAPPED`, `WSABUF`, IOCP port, socket storage
- STL container, allocator, mutex, thread, custom deleter storage
- `NrResult<T>` 같은 template value wrapper를 runtime public function signature에 노출하는 형태

`NrServer::Impl`은 위 internal object를 자유롭게 소유하고 wiring할 수 있다. public header는 `Impl*` 또는 equivalent pointer-sized owner만 가진다.

생성 실패처럼 caller가 처리할 수 있는 실패는 public constructor에서 늦게 숨기지 않는다. Runtime public shell은 기본 생성된 invalid holder와 explicit create 함수 조합을 사용한다.

```cpp
class NrServer final
{
public:
    PSNR_API NrServer() noexcept;
    PSNR_API ~NrServer() noexcept;

    NrServer(const NrServer&) = delete;
    NrServer& operator=(const NrServer&) = delete;

    PSNR_API NrServer(NrServer&& other) noexcept;
    PSNR_API NrServer& operator=(NrServer&& other) noexcept;

    [[nodiscard]] PSNR_API static NrStatus Create(const NrServerConfig& config, NrServer* outServer) noexcept;

    [[nodiscard]] PSNR_API bool IsValid() const noexcept;
    [[nodiscard]] PSNR_API NrStatus Start() noexcept;

private:
    struct Impl;
    explicit NrServer(Impl* impl) noexcept;

    Impl* impl_ = nullptr;
};
```

규칙:

- public constructor는 allocation, socket open, worker start 같은 실패 가능한 runtime work를 수행하지 않는다.
- `Create(config, &outObject) -> NrStatus`에서 allocation/config validation 실패를 즉시 반환한다.
- `outObject`가 이미 valid하면 기존 runtime을 조용히 대체하지 말고 `InvalidState`를 반환한다.
- default-constructed 또는 moved-from public shell에 대한 operation은 `InvalidState`를 반환한다.
- `NrResult<T>`는 `.cpp` 또는 internal header의 helper에서는 사용할 수 있지만, Runtime public header의 signature나 private layout에는 올리지 않는다.

Internal virtual interface는 `PSNR_API` 없이 internal header 또는 `Impl` 뒤에 둔다. public API가 필요해져도 virtual interface를 직접 export하기보다 public Pimpl shell, opaque id, lifetime owner handle 중 하나로 다시 설계한다.

이 규칙은 `NetworkRuntime` 프로젝트가 완료될 때까지 새 `Nr` public API를 추가할 때 기본 검토 기준으로 사용한다.

## 권장 형태: opaque id/value handle

외부 caller가 객체 identity만 들고 있으면 되는 경우에는 opaque handle을 우선 검토한다.

```cpp
struct RuntimeSessionHandle
{
    std::uint64_t value = 0;
};
```

opaque id/value handle은 lifetime owner가 아니다. 실제 lifetime은 Runtime module 내부 registry, pool, manager가 책임진다.

예:

- session id
- connection id
- timer id
- zone/channel id

## 권장 형태: lifetime owner handle

외부 caller가 구현 세부를 알면 안 되지만, pending resource를 정리할 책임은 타입으로 표현해야 할 때 사용한다.

예:

- pending recv/send operation handle
- queued payload handle
- timer registration handle
- deferred close/drain token

```cpp
class PendingIoHandle
{
public:
    PendingIoHandle() noexcept = default;

    PendingIoHandle(const PendingIoHandle&) = delete;
    PendingIoHandle& operator=(const PendingIoHandle&) = delete;

    PSNR_API PendingIoHandle(PendingIoHandle&& other) noexcept;
    PSNR_API PendingIoHandle& operator=(PendingIoHandle&& other) noexcept;
    PSNR_API ~PendingIoHandle() noexcept;

    [[nodiscard]] PSNR_API bool IsValid() const noexcept;
    PSNR_API void Reset() noexcept;

private:
    friend class PendingIoFactory;

    struct Impl;
    explicit PendingIoHandle(Impl* impl) noexcept;

    Impl* impl_ = nullptr;
};
```

`Impl`은 pool block, native handle, `OVERLAPPED`, `WSABUF`, queue linkage 같은 구현 세부를 가진다. public header는 lifetime 동작만 노출한다.

Native pointer가 필요한 함수는 public API가 아니라 internal helper에 둔다.

```cpp
OVERLAPPED* NativeOverlapped(PendingIoHandle& handle) noexcept; // internal only
WSABUF* NativeWsaBuffer(PendingIoHandle& handle) noexcept;      // internal only
```

## 내부 타입

IOCP completion 처리처럼 DLL 내부에서만 쓰는 타입은 export하지 않는다.

예:

- pending I/O context
- raw `OVERLAPPED` wrapper
- `WSABUF` view holder
- pool block metadata
- queue node

이 타입들은 internal header나 `.cpp` 구현에 둔다. 테스트가 필요해도 DLL public API를 넓히지 않는다. Public behavior는 DLL black-box로 검증하고, 직접 검증이 필요한 implementation은 ADR 0006의 Internal.lib seam을 사용한다.

예를 들어 pending I/O context는 외부 caller가 직접 생성하거나 조작할 객체가 아니다. 따라서 public `Context` class로 노출하지 않고, factory가 만든 lifetime owner handle 뒤에 숨긴다.

## Internal header

DLL 밖 caller가 직접 알 필요는 없지만, 여러 `.cpp` 파일이나 test project에서 같은 구현 타입을 검증해야 하면 internal header를 사용한다.

Internal header는 public SDK surface가 아니라 module-private contract다.

권장 사용처:

- public API로 노출하지 않을 lookup table, registry, adapter.
- queue, packet dispatch, session registry 내부 helper처럼 unit test가 필요한 구현 타입.
- STL container, fixed storage, Win32 storage, allocator, synchronization primitive를 멤버로 가질 수 있는 타입.

규칙:

- 파일명에 `Internal`을 붙여 의도를 드러낸다.
- class나 함수에 `PSNR_API`를 붙이지 않는다.
- DLL 외부 소비자가 include해야 하는 header에서 internal header를 include하지 않는다.
- internal header에 있는 타입은 ABI 안정성을 보장하지 않는다.
- 테스트도 기본적으로 product-facing caller처럼 public module/handle/value API만 사용한다.
- internal header 직접 include는 기존 테스트 현실 또는 명시적 예외 사유가 있을 때만 허용한다. 단, 링크가 막히는 내부 구현을 테스트하려고 `PSNR_API`를 추가하지 않는다.
- Internal.lib 분리 전 Test project의 source-root include는 안정적인 internal behavior test seam으로 보지 않는다. 분리 후 direct internal test는 Internal.lib와 명시된 internal include root를 사용한다.
- internal type이 외부 caller가 직접 호출해야 하는 행동으로 승격되면 public module + Pimpl, opaque id, lifetime owner handle 중 하나로 다시 설계한다.
- internal interface는 `PSNR_API` 없이 둘 수 있다. public API가 필요해지면 virtual interface 자체를 export하지 말고 Pimpl shell 안으로 숨기는 방향을 먼저 검토한다.

예:

```text
NrPacketDispatchTableInternal.h
  - std::array 기반 dispatch lookup storage
  - NetworkRuntime 내부 recv pipeline과 tests가 include
  - World Server나 external smoke caller가 직접 include하지 않음
```

이 규칙의 목적은 public export와 ABI coupling을 줄이면서도, `.cpp` local type으로 숨겨 테스트가 어려워지는 문제를 피하는 것이다.

## Win32 include 정책

Win32/Winsock header는 public header에 직접 반복하지 않는다.

- Win32 type을 public surface에 반드시 노출해야 하면 공통 wrapper header를 통해 include한다.
- `WinSock2.h`는 `Windows.h`보다 먼저 include한다.
- `WIN32_LEAN_AND_MEAN`, `NOMINMAX`는 공통 wrapper header에서 정의한다.

권장 wrapper:

```cpp
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <MSWSock.h>
#include <Windows.h>
```

## 적용 기준

새 NetworkRuntime public API를 만들 때는 먼저 다음을 확인한다.

1. DLL 밖 caller가 이 type의 행동을 직접 호출해야 하는가?
2. DLL 밖 caller가 identity만 들고 다시 넘기면 되는가?
3. DLL 밖 caller가 pending resource의 lifetime만 소유하면 되는가?
4. DLL 밖 caller가 이 type을 전혀 몰라도 되는가?
5. private member에 STL, Win32, allocator, mutex, custom deleter가 들어가는가?
6. 이 type의 구현을 바꿀 때 DLL 소비자 rebuild 없이 유지하고 싶은가?

1번이면 public module + Pimpl을 검토한다. 2번이면 opaque id/value handle을 사용한다. 3번이면 lifetime owner handle을 사용한다. 4번이면 internal type으로 둔다.

5번 또는 6번이 맞는데도 public class layout을 노출해야 한다면 설계가 잘못된 신호로 보고 다시 선택 순서부터 검토한다.

## 예외

다음 type은 public header에 직접 노출해도 된다.

- `enum class`
- fixed-width integer 기반 id/key
- POD config/stat snapshot
- `NrStatus` 같은 project-wide value wrapper
- 기존 core layer의 `NrResult<T>` 사용은 후속 DLL boundary cleanup 대상이다.

예외 type도 STL container나 platform storage를 member로 갖기 시작하면 이 규칙을 다시 적용한다.
