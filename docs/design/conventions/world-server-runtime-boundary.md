# World Server와 NetworkRuntime 경계 규칙

## 목적

World Server의 gameplay protocol, domain과 simulation이 NetworkRuntime의 transport 구현이나 public value type에 불필요하게 결합되지 않도록 반복 적용할 의존 규칙을 정의한다.

이 문서는 World Server 내부 모듈과 Runtime integration adapter 사이의 seam을 소유한다. NetworkRuntime DLL의 export, ABI와 Pimpl 규칙은 `dll-boundary.md`가 계속 소유한다.

## 의존 방향

```text
World protocol / domain / simulation
  World-owned fixed-width value와 error
                  |
                  v
       Runtime integration adapter
                  |
                  v
NetworkRuntime public SDK + DLL/import library
```

- World protocol, domain과 simulation은 NetworkRuntime 전용 타입을 자체 interface로 사용하지 않는다.
- Runtime integration adapter는 World-owned value를 public Runtime value로 변환하고, `NrToWorldEvent`와 `NrGateway`를 World ingress/outbound operation에 연결한다.
- Host는 NetworkRuntime과 WorldServer의 생성, 실행, drain과 shutdown 순서를 조립한다. Gameplay 의미나 protocol validation을 소유하지 않는다.

## World-owned protocol 계약

- Packet ID는 wire 크기를 보존하는 `std::uint16_t` 기반 `enum class`로 정의한다.
- C2S와 S2C는 각각 `C2SPacketType`, `S2CPacketType`으로 분리해 방향이 다른 packet을 컴파일 단계에서 혼용하지 못하게 한다.
- Packet enumerator는 `C2SPacketType::MovementInput`처럼 type이 방향과 역할을 표현하므로 이름에 `C2S`, `S2C`, `PacketType`을 반복하지 않는다.
- Payload version, enum, ID, tick과 wire DTO는 gameplay protocol 문서가 정한 fixed-width type을 사용한다.
- Protocol decode/encode 결과는 World-owned error로 표현한다.
- `NrPacketType`, `NrStatus`, `NrToWorldEvent`, `NrGateway`, `NrSessionKey` 같은 Runtime type을 protocol DTO, domain state나 simulation command의 interface에 넣지 않는다.
- Semantic payload codec은 Runtime 6-byte transport header를 읽거나 쓰지 않는다.
- Versioned wire DTO는 version namespace에 두고, decode 이후 version-neutral World command로 변환한다.

### 패킷 모듈 구성

- 공용 wire codec은 `ReadU16`, `WriteU16`, `ReadU32`, `WriteU32`, `ReadI16`, `WriteI16`, `ReadF32`, `WriteF32`처럼 fixed-width little-endian primitive만 제공한다.
- 중앙 codec class에 패킷별 `Encode`와 `Decode` overload를 누적하지 않는다.
- 각 versioned 패킷 구조체가 자신의 value field, `Wire` offset과 payload 크기, static `Encode`와 `Decode`, numeric/enum 검증을 함께 소유한다.
- 각 패킷의 `Wire` 선언 앞에는 field 경계와 offset을 한눈에 확인할 수 있는 ASCII layout을 둔다.
- 패킷 구조체의 static API는 `Packet::Encode(value, output)`과 `Packet::Decode(payload, outValue)` 형태로 통일한다.
- AOI 대상 선택, controlled entity 제외, record 중복 제거와 `WorldEntityKey` 정렬은 replication planner 책임이다. Codec은 결정된 batch의 wire layout과 field 값만 검증한다.

```text
Replication planner
  -> 대상 선택 / 중복 제거 / WorldEntityKey 정렬
  -> EntityStateBatch value
  -> EntityStateBatch::Encode
  -> WorldProtocolWireCodec primitive
  -> semantic payload bytes
```

```text
protocol::v1::MovementInput
  -> validation
  -> version-neutral MovementCommand
  -> tick input store
```

## Runtime integration adapter

다음 변환과 operation은 Runtime integration adapter 경계에서만 허용한다.

- `C2SPacketType` 또는 `S2CPacketType`의 underlying `std::uint16_t` 값에서 `NrPacketType`으로 변환
- Runtime의 `NrSessionKey` fixed-width 값에서 World-owned `WorldSessionKey`로 변환
- World protocol failure에서 Runtime close/status decision으로 변환
- `NrToWorldEvent`의 semantic payload view를 event lifetime 안에서 decode
- Decode된 value를 owned World DTO 또는 command로 전환
- World outbound semantic payload를 `NrGateway`에 제출

Adapter는 Runtime type을 World 내부로 전달하는 pass-through 계층이 아니다. Runtime lifetime과 error를 World-owned input/output으로 번역하는 seam이다.

## 허용하는 의존성

- `PrivateServer.WorldServer`와 Host는 staged NetworkRuntime public header를 include할 수 있다.
- Runtime integration adapter와 Host는 NetworkRuntime DLL/import library를 link할 수 있다.
- WorldServer static library 내부 type은 STL container와 `std::span`을 사용할 수 있다.
- Codec 단위 테스트는 WorldServer static library의 protocol interface를 직접 검증할 수 있다.
- Runtime integration test와 public loopback은 NetworkRuntime public header와 DLL/import library만 사용한다.

## World C++ API parameter 규칙

- World protocol, domain, registry와 simulation API도 `cpp-style.md`의 함수 매개변수 규칙을 따른다.
- 출력 parameter는 `T* outValue`를 사용하고 진입부에서 `nullptr`를 검사한다. `T&`는 필수 mutable 입력이나 dependency에만 사용하며 출력 용도로 사용하지 않는다.
- 호출자가 `&value`를 명시하도록 하여 결과 기록이 발생하는 호출임을 드러낸다.
- 출력값은 operation이 성공했을 때만 commit하고 실패 시 caller의 기존 값을 유지한다.

## 금지하는 의존성

- WorldServer, Host 또는 WorldServer Tests에서 NetworkRuntime internal header나 `PrivateServer.NetworkRuntime.Internal.lib`를 사용하지 않는다.
- Test link를 위해 WorldServer type에 `PSNR_API`를 추가하지 않는다.
- Runtime Session object, actor, mailbox, queue, socket, `OVERLAPPED`, `WSABUF` 또는 IOCP storage를 World type으로 노출하거나 보관하지 않는다.
- Protocol/domain/simulation API가 `NrResult<T>` 또는 Runtime internal concrete type을 반환하지 않는다.
- WorldServer static library의 내부 codec, registry나 simulation type을 외부 DLL API처럼 설계하지 않는다.

## 향후 WorldServer DLL 전환

현재 `PrivateServer.WorldServer`는 static library이며 Host와 같은 binary에 link된다. 따라서 World 내부 interface에는 NetworkRuntime DLL과 같은 ABI 제한을 적용하지 않는다.

향후 WorldServer를 별도 DLL로 전환한다면 기존 internal type을 그대로 export하지 않는다. 외부 caller가 필요한 행동과 lifetime을 다시 식별하고 다음 형태 중 하나로 별도 public surface를 설계한다.

- public module + Pimpl
- fixed-width opaque ID/value handle
- pointer-sized lifetime owner handle
- small config/status/snapshot value

STL container, allocator, synchronization primitive, platform storage와 mutable simulation layout은 DLL public class layout에 노출하지 않는다.

## 검토 체크리스트

- 새 protocol/domain/simulation header가 Runtime header를 include하는가?
- Runtime type이 World command, entity state 또는 tick input에 남아 있는가?
- Semantic codec이 transport header를 중복 처리하는가?
- Payload view가 `NrToWorldEvent` lifetime을 넘어 보관되는가?
- Integration test가 public SDK와 DLL/import library만 사용하는가?
- Test 편의를 위해 export나 internal dependency를 추가했는가?
- 새 World API의 출력 parameter가 `T* outValue`이고 실패 시 기존 값을 보존하는가?

관련 기준:

- `docs/design/conventions/cpp-style.md`
- `docs/design/conventions/dll-boundary.md`
- `docs/adr/0006-network-runtime-world-integration-contract.md`
- `docs/design/gameplay/gameplay-protocol-and-simulation-contract.md`
