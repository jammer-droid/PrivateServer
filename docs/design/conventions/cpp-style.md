# C++ 스타일 규칙

## 이름

```text
namespace     psnr::<module>
class/struct  PascalCase
enum class    PascalCase
enum value    PascalCase
function      PascalCase
bool fn       predicate/attempt/outcome vocabulary
local var     lowerCamelCase
parameter     lowerCamelCase
member var    lowerCamelCase_
constant      PascalCase
macro         UPPER_SNAKE_CASE
```

## 예시

```cpp
namespace psnr::core
{
enum class NrErrorCode
{
    Success,
    InvalidArgument,
    PoolExhausted,
};

class NrStatus
{
public:
    bool Succeeded() const noexcept;
    bool Failed() const noexcept;

private:
    NrErrorCode errorCode_;
};
}
```

`bool` 함수는 의미에 맞는 동사를 사용한다. 상태와 capability는 `Is`/`Has`/`Can`/`Should`, 성공 여부를 포함한 시도는 `Try`, 완료 결과는 `Succeeded`/`Failed`/`Accepted`, 비교 predicate는 `Matches`를 사용한다.

## 멤버 변수

멤버 변수는 trailing underscore를 사용한다.

```cpp
std::size_t blockSize_;
NrErrorCode errorCode_;
```

`m_` prefix는 사용하지 않는다.

## 타입 명시

C++ 코드에서는 `auto`를 사용하지 않고 변수와 반환값의 구체적인 타입을 명시한다. Review 지점에서 실제 owner, view와 value type이 바로 보이도록 하며, 타입이 길다는 이유만으로 alias나 type erasure를 추가하지 않는다.

## 포인터

포인터에 `p_` prefix를 사용하지 않는다.

소유권은 타입과 이름으로 표현한다.

```cpp
MemoryPool* ownerPool_;                    // non-owning
std::byte* data_;                          // non-owning view
std::unique_ptr<std::byte[]> storage_;     // owning
```

## 함수 매개변수

매개변수 타입은 입력 여부, 변경 가능성, 출력 여부를 드러내도록 선택한다.

| 역할 | 타입 |
| --- | --- |
| 작은 값, enum, ID, view | `T` |
| 필수 읽기 전용 입력 객체 | `const T&` |
| non-const 동작이 필요한 필수 입력 또는 dependency | `T&` |
| 출력 | `T* outValue` |
| 선택적 읽기 전용 입력 | `const T* optionalValue` |

작은 값의 함수 signature는 `T`로 선언한다. 함수 정의에서 값 매개변수를 재대입하지 않는다는 사실을 드러낼 필요가 있으면 top-level `const T`를 사용할 수 있다. Top-level `const`는 caller contract를 바꾸지 않는다.

복사할 필요가 없는 복합 객체를 필수 읽기 전용 입력으로 사용할 때는 `const T&`를 사용한다. 이 타입은 `nullptr`를 허용하지 않으며 해당 참조를 통해 `const` member function만 호출할 수 있다.

```cpp
NrStatus Connect(const NrEndpoint& endpoint) noexcept;
```

필수 dependency의 non-const 동작을 호출해야 한다면 `T&`를 사용한다. 함수가 결과를 기록하는 out parameter와 구분하기 위해 `T&`를 출력 용도로 사용하지 않는다.

```cpp
void Register(NrSessionRegistry& registry) noexcept;
```

출력 parameter는 `T*`를 사용하고 이름을 `out`으로 시작한다. 호출자는 `&`를 명시해 함수가 해당 객체에 결과를 기록한다는 사실을 호출 지점에서 드러낸다.

```cpp
NrClient client;
const NrStatus status = NrClient::Create(config, &client);
```

public function과 subsystem 진입점은 out pointer의 `nullptr`를 진입부에서 한 번 검사하고 `InvalidArgument`를 반환한다. 검증된 pointer를 전달받는 private helper는 같은 검사를 반복하지 않으며, 내부 invariant 확인이 필요하면 `assert`를 사용한다. Out value는 기본적으로 operation이 성공했을 때만 변경하고 실패 시 caller의 기존 값을 유지한다.

Failure classification이나 diagnostic detail 자체가 operation 결과의 일부라면 문서에 열거한 failure status에서도 완성된 out result를 commit할 수 있다. 예를 들어 packet parser는 `ProtocolError`와 함께 protocol reason을 반환할 수 있다. 이 예외도 partial write를 허용하지 않으며, local result를 완성한 뒤 한 번에 commit하고 어떤 status에서 output이 유효한지 contract에 명시한다.

선택적 읽기 전용 입력에만 `const T*`를 사용한다. `nullptr`는 입력이 제공되지 않았음을 뜻하며, non-null일 때도 해당 pointer를 통해 `const` member function만 호출할 수 있다. 입력이 필수라면 pointer 대신 `const T&`를 사용한다.

```cpp
void WriteDiagnostic(const NrDiagnosticContext* optionalContext) noexcept;
```

함수 매개변수로 전달되는 참조와 raw pointer는 기본적으로 소유권을 갖지 않는다. 함수나 객체가 이를 호출 이후에도 보관하면 referent가 더 오래 살아야 한다는 lifetime 계약을 문서화한다.

## 객체 소유권과 lifetime

객체를 생성하고 소멸할 책임은 member type으로 표현한다.

| member type | 의미 | owner 소멸 시 referent 소멸 |
| --- | --- | --- |
| `T value_` | 값으로 직접 소유 | yes |
| `T& dependency_` | 필수 non-owning dependency | no |
| `T* optionalDependency_` | nullable non-owning dependency | no |
| `std::unique_ptr<T> owner_` | 단독 소유 | yes |
| `Impl* impl_` | DLL Pimpl/lifetime-owner shell의 private owner와 invalid-state storage | yes |
| `T* guardedBorrow_` | lease/guard가 lifetime을 pin하는 nullable borrow | no |

생성자에서 받은 `T&`를 참조 member로 보관해도 referent의 소유권은 이전되지 않으며, owner가 소멸할 때 referent의 소멸자를 호출하지 않는다. Referent는 이를 보관하는 객체보다 오래 살아야 한다.

```cpp
class NrClientTransport final
{
public:
    explicit NrClientTransport(NrIocpPort& iocpPort) noexcept
        : iocpPort_(iocpPort)
    {
    }

private:
    NrIocpPort& iocpPort_; // NrIocpPort가 NrClientTransport보다 오래 살아야 한다.
};
```

외부 객체의 lifetime에 의존할 필요가 없다면 값을 복사하거나 owning type으로 전달받는다. 일반 구현 객체의 동적 소유권은 값 또는 `std::unique_ptr`로 표현하며 ordinary raw pointer를 owner로 사용하지 않는다. 소유하지 않는 객체를 임의로 `delete`하지 않는다.

DLL public Pimpl shell과 DLL public lifetime-owner handle이 ABI에 pointer-sized storage만 노출해야 하는 경우의 private `Impl*`은 예외다. 이 pointer의 생성, move, reset과 파괴는 shell의 out-of-line DLL 함수가 전부 책임지고, default-constructed 또는 moved-from shell에서는 `nullptr`가 명시적인 invalid state를 나타낸다. 상세 ABI 규칙은 `dll-boundary.md`를 따른다.

Runtime/product code에서는 일반 객체의 owner와 소멸 책임을 흐리는 `std::shared_ptr`를 사용하지 않는다. 객체는 값 또는 `std::unique_ptr`로 단독 소유하고, 다른 component는 참조, non-owning pointer, ID 또는 명시적인 lifetime handle을 사용한다.

비동기 작업 사이에서 특정 resource의 lifetime을 공유해야 하면 일반 객체 전체를 `std::shared_ptr`로 감싸지 않는다. `NrPayloadRef`처럼 목적과 release 조건이 제한된 project type을 사용한다.

Purpose-limited lifetime handle은 private raw control pointer로 ref 또는 pool lease를 표현할 수 있다. 이 raw pointer는 일반 객체 owner로 외부에 노출하지 않으며, handle type이 retain/share, move, reset, 마지막 release와 empty state를 완전히 캡슐화해야 한다. `nullptr`는 default-constructed, moved-from 또는 released handle을 나타내므로 이러한 control pointer를 기계적으로 reference member로 바꾸지 않는다.

- lifetime을 보호하는 대상과 실제 owner를 명확히 한다.
- mutable object 전체의 공동 ownership을 제공하지 않는다.
- 복사를 통한 암묵적 공유 대신 `Share()`처럼 공유 동작을 명시한다.
- 마지막 ref가 해제되는 시점과 pool/resource 반환 책임을 type contract로 고정한다.
- session과 actor 같은 lifecycle owner 자체는 registry 또는 composition root가 단독 소유한다.

```cpp
NrPayloadRef sharedPayload = payload.Share();
```

Lease 또는 guard type은 guarded borrow를 private raw pointer로 보관할 수 있다. Pointer 자체는 referent를 소유하지 않지만 guard가 active lease count, admission ticket 또는 동등한 pin을 소유해 유효한 동안 referent lifetime을 보장한다. 이러한 type은 empty, moved-from 또는 reset 상태를 가져야 하므로 reference member로 바꾸지 않는다. Acquire 경계에서 유효성을 확인하고 valid guard의 일반 operation에서는 같은 null 검사를 반복하지 않는다.

Test code도 같은 원칙을 기본으로 하며, 비동기 test fixture의 관측 상태처럼 공동 lifetime이 반드시 필요한 예외는 해당 이유가 test type에 드러나야 한다.

생성에 성공한 객체는 일반 operation에 필요한 필수 dependency를 모두 가진 usable invariant를 만족해야 한다. Allocation, native resource 획득, worker 시작처럼 실패할 수 있는 작업은 constructor에서 숨기지 않고 project의 `Create` 또는 명시적인 initialization 경계에서 처리한다.

Default-constructed 또는 moved-from 상태를 public contract로 제공하는 Pimpl shell은 예외적으로 invalid state를 가질 수 있다. 이 경우 `IsValid()`와 operation의 `InvalidState` 반환 규칙을 명시한다.

## null과 invariant 검사

`nullptr`는 optional dependency, 빈 owner, moved-from object, 아직 생성되지 않았거나 이미 해제된 resource처럼 실제 domain/lifecycle 상태를 나타낼 때만 사용한다. 필수 dependency의 부재를 정상 operation마다 `nullptr` 검사로 보완하지 않는다.

검사는 책임 경계에 둔다.

- public function과 subsystem 진입점은 외부 입력, out pointer와 handle을 검증한다.
- `Create`와 initialization 경계는 필수 dependency와 resource 획득 결과를 검증한다.
- allocation 함수와 Win32/Winsock API가 반환한 pointer 또는 handle의 실패 결과는 반드시 검증한다.
- construction이 완료된 객체의 일반 operation은 확립된 invariant를 전제로 동작하며 같은 null 검사를 반복하지 않는다.
- private helper가 내부 invariant를 확인할 필요가 있으면 `assert`를 사용한다.
- `static_assert`는 type trait, 크기, 정렬과 layout 같은 compile-time 조건에만 사용한다. 런타임 객체의 존재 여부를 검사하는 용도로 사용하지 않는다.

```cpp
NrStatus CreateComponent(const NrComponentConfig& config, NrComponent* outComponent) noexcept
{
    if (outComponent == nullptr)
    {
        return NrStatus::Failure(NrErrorCode::InvalidArgument);
    }

    NrComponent component;
    // 생성 경계에서 config, allocation과 필수 dependency를 검증하고 component를 완성한다.

    *outComponent = std::move(component); // 성공 직전에 결과를 commit한다.
    return NrStatus::Success();
}
```

`assert`는 release build의 외부 입력 검증을 대체하지 않는다. 외부 caller가 위반할 수 있는 contract는 명시적인 status로 반환하고, 프로그램 내부에서 이미 성립해야 하는 조건만 assertion으로 확인한다.

## DLL 경계

DLL 밖에서 호출해야 하는 out-of-line public 함수에만 export macro를 붙이는 것을 기본으로 한다.

상세 규칙은 `dll-boundary.md`를 따른다.

```cpp
class NrServer
{
public:
    PSNR_API ~NrServer() noexcept;
    [[nodiscard]] static PSNR_API NrStatus Create(const NrServerConfig& config,
                                                  NrServer* outServer) noexcept;
};
```

STL container나 custom deleter를 가진 멤버가 있는 class 전체에 export macro를 붙이지 않는다.

```cpp
class PSNR_API MemoryPool // avoid
{
private:
    std::vector<std::size_t> freeBlockIndices_;
    std::unique_ptr<std::byte, StorageDeleter> storage_;
};
```

class 전체를 export하면 private member도 객체 layout의 일부로 DLL 사용자에게 노출되고, MSVC C4251 경고와 ABI coupling이 생길 수 있다. 외부 ABI 안정성이 중요한 타입은 Pimpl 또는 opaque handle을 검토한다.

## 포매팅

포매팅은 `.clang-format`를 따른다.
