#pragma once

#include <array>
#include <cstdint>

namespace psnr::world::protocol
{
    /*
    전체 packet은 NetworkRuntime transport header와 World semantic payload로 구성된다.
    World protocol codec은 semantic payload만 읽고 쓰며 6-byte transport header는 다루지 않는다.

    Runtime frame
    +------------------+------------------+----------------+----------+----------------------+
    | packetLength 2B  | packetType 2B    | version 1B     | flags 1B | semantic payload ... |
    +------------------+------------------+----------------+----------+----------------------+
                                                 World codec 시작 -----^

    Client -> NetworkRuntime -> World Server
      C2S packet type -> WorldIngress routing -> payload decode -> World command

    World Server -> NetworkRuntime -> Client
      World result -> payload encode -> NrGateway submit -> S2C packet
    */

    enum class C2SPacketType : std::uint16_t
    {
        // Client가 World 참가를 요청한다.
        JoinWorldRequest = 0x0100,

        // Client가 controlled entity의 특정 server tick 이동 의도를 제출한다.
        MovementInput = 0x0101,

        // Client가 World server tick 추정을 위한 time-sync probe를 보낸다.
        WorldTimeSyncRequest = 0x0102,

        // Client가 controlled entity의 turn/boost control state 변경을 제출한다.
        ControlStateCommand = 0x0103,
    };

    enum class S2CPacketType : std::uint16_t
    {
        // Server가 초기 World baseline과 controlled entity 정보를 확정한다.
        WorldReady = 0x0180,

        // Client가 표현할 entity replica 생성을 알린다.
        EntitySpawn = 0x0181,

        // Client가 제어하는 entity의 authoritative state를 전달한다.
        ControlledEntityState = 0x0182,

        // AOI 안 remote entity들의 authoritative state 묶음을 전달한다.
        EntityStateBatch = 0x0183,

        // AOI 이탈 또는 entity lifetime 종료로 replica를 제거하도록 알린다.
        EntityRemove = 0x0184,

        // Authoritative player score 전체 값을 전달한다.
        ScoreState = 0x0185,

        // Authoritative round phase와 종료 조건을 전달한다.
        RoundState = 0x0186,

        // Client의 time-sync probe에 마지막 완료 server tick으로 응답한다.
        WorldTimeSyncResponse = 0x0187,

        // 같은 player/session의 controlled entity가 새 key로 교체됐음을 알린다.
        ControlledEntityRebind = 0x0188,

        // V2 chunked channel overview, ActiveArea, silhouettes and leaderboard.
        WorldOverviewSnapshot = 0x0189,

        // Final authoritative round result for one recipient.
        RoundResult = 0x018A,
    };

    /*
    NetworkRuntime은 gameplay packet의 의미를 해석하지 않으므로 server 시작 시 C2S gameplay packet type을
    WorldIngress 대상으로 등록해야 한다. Runtime integration adapter는 이 World-owned 값을 NrPacketType으로
    변환해 NrServerConfig.additionalWorldIngressPacketTypes에 전달한다.

    이 배열은 routing catalog다.
    - 포함된 type: Runtime이 NrToWorldEventKind::PacketReceived event로 World에 전달
    - 포함되지 않은 type: 다른 lane 규칙을 따르거나 등록되지 않은 packet으로 거절
    - 하지 않는 일: payload version/length/enum/numeric validation과 World command admission
    */
    inline constexpr std::array<C2SPacketType, 4> C2SWorldIngressPacketTypes = {
        C2SPacketType::JoinWorldRequest,
        C2SPacketType::MovementInput,
        C2SPacketType::WorldTimeSyncRequest,
        C2SPacketType::ControlStateCommand,
    };
} // namespace psnr::world::protocol
