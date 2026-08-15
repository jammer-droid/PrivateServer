# Private Server Domain Context

이 문서는 `PrivateServer`에서 반복해서 사용하는 프로젝트 고유 언어와 경계를 정의한다. 작업 절차는 `AGENTS.md`, 기술 결정은 ADR과 convention, 현재 구조 설명은 `wiki/`가 담당한다.

## 프로젝트 경계

- **Private Server**: 현실적인 MMO-lite 서버 구조를 구현·검증·설명하는 프로젝트.
- **C++/IOCP Primary World Server**: Windows-native networking, IO worker, authoritative gameplay, 부하·운영 증거를 보여주는 주 구현 대상.
- **Thin 2D Visualization Client**: 서버가 소유한 상태를 표현하는 경량 client. Gameplay 권위와 핵심 복잡도를 소유하지 않는다.
- **Single Logical World**: client에게 연속된 하나의 공간처럼 보이지만 server 내부에서는 region, worker 또는 process로 나눌 수 있는 world.

현재 범위는 production MMORPG 규모, seamless MMO/server meshing, 자동 cloud scale-out, seamless live migration을 포함하지 않는다.

## World와 gameplay

- **World Server**: movement, AOI, combat, shop, inventory, zone/channel 상태를 권위 있게 소유하는 server.
- **World Server Main Loop**: 핵심 world simulation phase와 canonical commit을 조정하는 논리적 권위 실행 loop. 단일 logical owner는 모든 drain, 계산과 publication을 하나의 OS thread에서 실행한다는 뜻이 아니다.
- **World Coordinator**: server tick과 buffer epoch를 소유하고 immutable input 확정, phase 조정과 canonical commit을 수행하는 World Main Loop의 권위 owner.
- **World Ingress Pump**: ToWorld queue에서 owning event를 drain해 World의 write slot으로 옮기는 단일 consumer. Gameplay payload를 해석하거나 canonical World state를 변경하지 않는다.
- **World Outbound Publisher**: World가 완성한 outbound batch를 읽어 NetworkRuntime gateway에 제출하는 단일 publication worker. World state나 gameplay rule을 소유하지 않는다.
- **Region**: Single Logical World를 server ownership과 부하 관리 목적으로 나눈 단위.
- **Entity Ownership**: gameplay entity마다 mutable state를 변경할 수 있는 authoritative owner가 한 시점에 정확히 하나라는 규칙.
- **Owner Queue / Owner Mailbox**: entity, room, zone, channel 또는 session owner에게 command/event를 전달하는 queue.
- **Tick / Update Cadence**: AI, cooldown, AOI, replication, timer, persistence, metrics를 주기적으로 진행하는 주기. 모든 client input의 기본 대기 경계는 아니다.
- **Ghost Entity**: 인접 region의 AOI나 경계 상호작용을 위해 복제된 제한적·읽기 중심 entity view.
- **Hotspot**: population, AOI fanout, tick time 또는 queue depth가 높아 운영 대응이 필요한 region/cell.
- **Zone**: Town, Dungeon 같은 논리적 world area.
- **Channel**: Zone을 확장하기 위한 독립 instance. NetworkRuntime의 send channel과 다른 개념이다.
- **Channel ID**: 한 World Host가 실행하는 Channel을 식별하는 non-zero identity. Host 실행마다 바뀌는 Run ID나 Channel 내부 Player ID와 다른 값이다.
- **Channel Directory**: client가 선택할 Channel의 ID, 표시 이름과 endpoint를 제공하는 목록. 현재 local 정적 설정이며 future World Manager가 같은 역할을 대체할 수 있다.
- **World Session Role**: accepted Runtime Session의 World 참여 상태를 Connected, Player 또는 Observer 중 하나로 구분하는 배타적 권한.
- **Connected Session**: World에 등록됐지만 아직 Player나 Observer로 admission되지 않은 Runtime Session.
- **Player Session**: Player ID와 controlled entity를 binding하고 gameplay input과 상세 AOI replication에 참여하는 World Session.
- **Observer Session**: Player ID, controlled entity와 gameplay input 권한 없이 Channel의 World overview와 round state/result를 읽는 World Session.
- **Controlled Handoff**: 명시적인 zone/channel 전환. Seamless handoff를 뜻하지 않는다.
- **AOI**: 각 client가 관측할 nearby entity 집합을 server가 결정하는 경계.
- **Map Bounds**: Channel World가 사용하는 고정 좌표 경계. Round 중 줄어드는 생존 영역과 구분한다.
- **Active Area**: Round 진행에 따라 크기가 바뀔 수 있는 server-authoritative 생존 가능 영역. AOI나 Map Bounds와 다른 gameplay 규칙이다.
- **AOI Viewpoint**: Player Session의 controlled entity를 중심으로 상세 entity replica 집합을 고르는 공간 경계. Observer Session이나 Active Area와 다른 개념이다.
- **World Broadcast**: World Server가 recipient 집합을 계산한 뒤 같은 world event를 여러 runtime session에 보내는 결정.
- **Gameplay Protocol**: NetworkRuntime transport header와 분리된 game-specific packet type, semantic payload layout과 validation 계약.
- **World Entity Key**: client replication과 World command correlation에 사용하는 `entity ID + generation` identity. Registry slot이나 인증 token이 아니다.
- **Controlled Entity**: Runtime Session Key에 연결되어 해당 session의 input actor로 해석되는 World entity.
- **World Entity Registry**: World Entity Key의 발급, lifetime과 내부 handle resolution을 소유하는 World-side registry.
- **Physics Proxy**: World entity가 소유하는 server-internal collision/query 표현. 하나의 entity가 여러 proxy를 가질 수 있으며 wire identity가 아니다.
- **Body Trail Sample**: 연속 body 중심선을 재구성하기 위해 server가 보관하는 head 이동 경로 표본. Art segment나 독립 World entity가 아니다.
- **Growth Point**: resource 획득과 boost 소모로 바뀌며 현재 body 성장량과 score를 함께 결정하는 server-authoritative gameplay 값.
- **Player Display Name**: 한 Channel 참가 session에서만 사용하는 선택적 영문·숫자 표시값. Player ID, account identity 또는 인증 credential이 아니다.

## Actor 실행과 ownership

- **Actor Executor**: actor mailbox를 실행하되 한 actor의 mutable state는 한 consumer만 drain하도록 보장하는 공유 실행 primitive.
- **Actor Interface**: runtime session actor와 future world owner actor가 executor에 제공하는 공통 계약.
- **Actor Mailbox**: 여러 producer가 event를 넣고 해당 actor의 단일 drain owner만 소비하는 actor-owned inbox.
- **Actor Mailbox Handle**: actor mutable state나 drain API를 노출하지 않는 producer-facing enqueue capability.
- **Actor Schedule Request**: mailbox work가 actor를 runnable 상태로 만들었을 때 executor에 실행 기회를 요청하는 값.
- **Actor Ready Queue**: Actor Schedule Request를 저장하는 executor queue. Actor의 domain message를 저장하지 않는다.
- **Actor Scheduling State**: actor가 idle, scheduled, draining인지와 drain 중 도착한 work가 있는지를 나타내는 상태.
- **Actor Slot**: actor, mailbox capability, scheduling/lifetime state, generation/session identity를 함께 보관하는 runtime-owned storage cell.
- **Drain Ownership**: 한 actor의 mailbox를 drain하고 mutable state를 변경할 수 있는 일시적 단일 권한.

## NetworkRuntime 경계

- **NetworkRuntime**: client connection IO, runtime session lifecycle, inbound parsing, outbound framing, recv/send buffer lifetime을 소유하는 Windows-native C++/IOCP runtime.
- **Nr Prefix**: NetworkRuntime public type을 나타내는 prefix. `NrServer`처럼 역할을 바로 붙이며 `NrRuntime*`처럼 의미를 반복하지 않는다.
- **Runtime Session**: 한 client TCP connection을 나타내며 socket, IO, parser, buffer, close/drain 상태를 조정하는 NetworkRuntime 객체.
- **Runtime Session Key**: process lifetime 동안 재사용하지 않는 Runtime Session identity. Player/account/zone/channel identity가 아니다.
- **Runtime Session Actor**: 한 Runtime Session의 mutable recv/send/pending IO/close 상태를 직렬화하는 actor owner.
- **Runtime Session Actor Registry**: Runtime Session Key를 lifetime-safe actor handle 또는 mailbox에 연결하는 `NrServer` 내부 registry.
- **Dispatch Lane**: packet parsing 이후 input의 책임 경계를 나타내는 ServerIngress, SessionIngress, WorldIngress 구분.
- **Server Ingress**: heartbeat, disconnect, drain, shutdown 같은 runtime/operation input lane.
- **Session Ingress**: world ownership으로 해석되기 전 client session-owned input lane.
- **World Ingress**: 이후의 owner mailbox나 tick 구조를 결정하지 않은 채 World Server로 넘기는 gameplay/simulation input lane.
- **Ingress**: Dispatch Lane input을 받아 queue, batch 또는 후속 handler로 전달하는 입력 경계.
- **Handler**: dispatch된 input을 drain해 실제 runtime, control 또는 gameplay 동작을 수행하는 코드.
- **Packet Dispatch Rule**: PacketType을 Dispatch Lane, routing key, policy, priority에 연결하는 규칙.
- **To-World Event / NrToWorldEvent**: NetworkRuntime이 소유권을 가진 채 World Server로 전달하는 session lifecycle 또는 WorldIngress event.
- **Semantic Payload**: NetworkRuntime transport header를 제외한 application/gameplay payload bytes.
- **NrGateway**: World가 semantic payload와 packet type을 제출하고 NetworkRuntime이 framing과 fanout을 소유하게 하는 server-bound public 경계.
- **Session Send Channel / NrSessionSendChannel**: 한 Runtime Session으로 client-bound output을 제출하는 send-only capability. Close 권한이나 player identity를 포함하지 않는다.
- **Session Close Control**: Runtime Session Key와 close request reason으로 close/drain을 요청하는 별도 operation. Send capability와 분리한다.
- **World-side Runtime Session View**: World가 보관하는 Runtime Session Key와 send channel의 결합. Player/account identity 자체가 아니다.
- **Multi-recipient Send Submit**: 이미 계산된 여러 session send target에 하나의 semantic payload를 제출하는 NetworkRuntime 경계.
- **Send Fanout**: Multi-recipient submit을 per-session delivery attempt로 확장하는 NetworkRuntime 단계.

## 관측과 pressure

- **NrStatus**: 현재 operation의 성공·실패 분기를 위한 compact error/native code value.
- **Server Snapshot / NrServerSnapshot**: lifecycle, pending IO/session, queue/pool pressure를 담는 immutable observation value. Recovery policy를 소유하지 않는다.
- **Structured Diagnostic**: `NrStatus`를 키우지 않고 개별 failure/transition의 component, operation, runtime context를 남기는 debug/benchmark evidence record.
- **Backpressure Coordinator**: lane별 paused Runtime Session과 bounded resume를 조정하는 NetworkRuntime component 후보.

`NrStatus`는 control flow, `NrServerSnapshot`은 현재 상태와 누적 집계, Structured Diagnostic은 개별 occurrence의 시간순 context를 담당한다.

## 서비스와 운영

- **Outgame Server**: login, session, character selection 같은 world 진입 전 surface.
- **World Manager**: world server allocation을 알고 player를 zone/channel로 연결하는 service.
- **Chat Server**: player messaging을 담당하는 후속 독립 service 후보.
- **Control Plane**: server lifecycle, allocation, drain, backup, shutdown, bot load, scale-out을 관측·제어하는 local operation tool.
- **Game Server Fleet**: Registry/World Manager를 통해 시작·drain·allocation되는 여러 game server process/service.
- **Runtime Provider**: local process/Docker 실행을 제공하고 future hosting 확장을 숨기는 abstraction.
- **Windows CI/CD Deployment**: artifact를 Windows host에 배포하고 service/process runtime을 교체하는 release flow.
- **Reconnect-based Rolling Scale-out**: drain과 allocation 변경 후 reconnect/migration ticket으로 새 server에 재연결하는 확장 방식.
- **Maintenance Scale-out**: 승인된 maintenance window에서 drain, backup, shutdown, restart, reallocation으로 수행하는 확장 방식.
