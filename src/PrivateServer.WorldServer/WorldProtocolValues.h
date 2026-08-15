#pragma once

#include <cstdint>

namespace psnr::world::protocol
{
    enum class WorldProtocolError
    {
        Success,
        InvalidArgument,
        InvalidLength,
        UnsupportedVersion,
        InvalidEnum,
        InvalidNumeric,
    };

    enum class EntityKind : std::uint16_t
    {
        Invalid = 0,
        Player = 1,
        Resource = 2,
        StaticObstacle = 3,
    };

    enum class ShapeKind : std::uint16_t
    {
        Invalid = 0,
        Circle = 1,
    };

    enum class EntityRemoveReason : std::uint16_t
    {
        Invalid = 0,
        LeftAoi = 1,
        Destroyed = 2,
        Collected = 3,
        SessionClosed = 4,
        RoundReset = 5,
    };

    enum class RoundPhase : std::uint16_t
    {
        Invalid = 0,
        Waiting = 1,
        Running = 2,
        Ended = 3,
    };

    namespace v1
    {
        inline constexpr std::uint16_t PayloadVersion = 1;
    } // namespace v1

    namespace v2
    {
        inline constexpr std::uint16_t PayloadVersion = 2;

        enum class TurnState : std::uint16_t
        {
            Invalid = 0,
            Straight = 1,
            Left = 2,
            Right = 3,
        };

        enum class BoostState : std::uint16_t
        {
            Invalid = 0,
            Off = 1,
            On = 2,
        };
    } // namespace v2

    namespace v3
    {
        inline constexpr std::uint16_t PayloadVersion = 3;
    } // namespace v3
} // namespace psnr::world::protocol
