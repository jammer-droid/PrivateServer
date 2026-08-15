#pragma once

#include "WorldDoubleBufferRoleExchange.h"
#include "WorldResult.h"

#include <PrivateServer/NetworkRuntime/NrToWorldEvent.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace psnr::world
{
    enum class WorldIngressDoubleBufferExchangeResult : std::uint8_t
    {
        Exchanged = 0,
        InvalidArgument,
        InvalidState,
        Busy,
        TimedOut,
        Closed,
    };

    struct WorldIngressWriteBatch final
    {
        WorldDoubleBufferWriteClaim claim{};
        std::span<psnr::runtime::NrToWorldEvent> events{};
    };

    struct WorldIngressReadBatch final
    {
        WorldDoubleBufferReadClaim claim{};
        std::uint64_t epoch = 0;
        std::span<const psnr::runtime::NrToWorldEvent> events{};
    };

    // DoubleBuffered inbound 전용 A/B payload storage다.
    // 역할 전환과 claim 동기화는 WorldDoubleBufferRoleExchange가 담당한다.
    class WorldIngressDoubleBuffer final
    {
    public:
        WorldIngressDoubleBuffer(const WorldIngressDoubleBuffer&) = delete;
        WorldIngressDoubleBuffer& operator=(const WorldIngressDoubleBuffer&) = delete;

        [[nodiscard]] static WorldResult<std::unique_ptr<WorldIngressDoubleBuffer>> Create(
            std::size_t eventCapacityPerSlot) noexcept;

        [[nodiscard]] WorldResult<WorldIngressWriteBatch, WorldDoubleBufferRoleExchangeError> WaitAcquireWrite(
            std::uint64_t minimumGeneration, std::chrono::milliseconds timeout) noexcept;
        [[nodiscard]] WorldResult<void, WorldDoubleBufferRoleExchangeError> CommitWrite(
            const WorldIngressWriteBatch& batch, std::size_t eventCount) noexcept;
        [[nodiscard]] WorldResult<void, WorldDoubleBufferRoleExchangeError> WaitSwap(
            std::uint64_t epoch, std::chrono::milliseconds timeout) noexcept;
        [[nodiscard]] WorldResult<WorldIngressReadBatch, WorldDoubleBufferRoleExchangeError> WaitAcquireRead(
            std::uint64_t epoch, std::chrono::milliseconds timeout) noexcept;
        [[nodiscard]] WorldResult<void, WorldDoubleBufferRoleExchangeError> ReleaseRead(
            const WorldIngressReadBatch& batch) noexcept;

        [[nodiscard]] std::size_t EventCapacityPerSlot() const noexcept;
        void Close() noexcept;

    private:
        struct Slot final
        {
            explicit Slot(std::size_t eventCapacity);

            std::vector<psnr::runtime::NrToWorldEvent> events;
            std::size_t eventCount = 0;
        };

        explicit WorldIngressDoubleBuffer(std::size_t eventCapacityPerSlot);

        std::array<Slot, WorldDoubleBufferRoleExchange::SlotCount> slots_;
        WorldDoubleBufferRoleExchange roleExchange_;
    };
} // namespace psnr::world
