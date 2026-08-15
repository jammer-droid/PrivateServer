# 실패 처리 규칙

## 원칙

예상 가능한 실패는 예외로 처리하지 않는다.

작업 결과는 명시적으로 반환한다.

## Result

Core/internal에서 값을 만드는 작업은 `NrResult<T>`를 반환한다.

```cpp
NrResult<NrPooledMemoryBlock> result = memoryPool.AcquireBlock();
```

Runtime DLL public function이 값을 돌려줄 때는 `NrResult<T>` 대신 `NrStatus + out value/report`를 사용한다.

값이 없는 작업은 `NrStatus`를 반환한다.

```cpp
NrStatus status = session.PostRecv();
```

성공은 `NrErrorCode::Success`로 표현한다.

실패는 `NrErrorCode::Success` 외 값으로 표현한다.

`NrStatus`는 별도 success bool을 갖지 않는다.

## NrErrorCode

Runtime 공통 오류 코드는 하나의 enum으로 시작한다.

```cpp
enum class NrErrorCode
{
    Success,
    InvalidArgument,
    InvalidState,
    OutOfMemory,
    PoolExhausted,
    CapacityExceeded,
    IoFailed,
    OperationCanceled,
    ProtocolError,
};
```

Windows/Winsock 원본 오류는 `NrNativeErrorCode`로 보관한다.

```cpp
using NrNativeErrorCode = std::uint32_t;
```

## Bool

`bool`은 상태 질문에만 사용한다.

```cpp
bool valid = block.IsValid();
bool empty = buffer.IsEmpty();
```

작업 실패 표현에는 `bool`을 쓰지 않는다.

## Optional

`std::optional<T>`는 없음이 정상 결과일 때만 사용한다.

```cpp
std::optional<NrSessionKey> sessionKey = sessions.Find(socket);
```

`NrResult<T>` 내부 value storage는 `std::optional<T>`로 시작한다.

## 생성

실패 가능한 생성은 constructor가 아니라 `Create()`에서 처리한다.

```cpp
NrResult<std::unique_ptr<NrMemoryPool>> pool = NrMemoryPool::Create(config);
```

## Value 접근

`Value()`와 `TakeValue()`는 성공 상태에서만 호출한다.

```cpp
T& Value() noexcept;
T TakeValue() noexcept;
```

`TakeValue()`는 내부 값을 move로 꺼낸다.

```cpp
return std::move(*value_);
```

`TakeValue()` 호출 후 result 내부 value는 다시 사용하지 않는다.

## Precondition

precondition은 함수 호출 전 반드시 만족해야 하는 조건이다.

precondition 위반은 runtime 실패가 아니라 caller bug로 본다.

debug build에서는 assert로 잡는다.

release build에서는 precondition 만족을 전제로 동작한다.

## 정리

RAII 정리 함수는 `noexcept`로 둔다.

```cpp
~NrPooledMemoryBlock() noexcept;
void Reset() noexcept;
```

`noexcept` 함수는 예외를 밖으로 내보내지 않는다.
- `noexcept` 함수 내부에서 오류가 발생하면 이는 코드 버그 / 불변식 위반으로 보고 처리한다.

## 외부 오류

Windows/Winsock 오류는 NetworkRuntime boundary에서 내부 오류 코드로 변환한다.

```text
WSAGetLastError()
  -> NrErrorCode
```

## Structured diagnostics

`NrStatus`는 control-flow용 compact value로 유지한다.

- 문자열 message, component name, operation name, session context를 `NrStatus`에 넣지 않는다.
- 상세 failure context는 별도 structured diagnostic record로 남긴다.
- `NrServerSnapshot`은 누적 counter와 현재 상태를 제공하고, diagnostic record는 개별 occurrence의 시간 순서와 context를 제공한다.
- diagnostics failure가 원래 operation의 상태 전이를 바꾸면 안 된다.
- record/sink/queue contract는 `docs/design/refactoring/network-runtime-diagnostics-plan.md`에서 결정한다.
