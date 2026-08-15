#pragma once

#include "WorldResult.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace psnr::world
{
    enum class WorldDoubleBufferRoleExchangeError : std::uint8_t
    {
        InvalidArgument = 0,
        InvalidState,
        TimedOut,
        Closed,
    };

    struct WorldDoubleBufferWriteClaim final
    {
        std::size_t slotIndex = 2;
        std::uint64_t generation = 0;
    };

    struct WorldDoubleBufferReadClaim final
    {
        std::size_t slotIndex = 2;
        std::uint64_t generation = 0;
        std::uint64_t epoch = 0;
    };

    // Payload를 소유하지 않고 두 slot의 read/write 역할과 교환 경계만 관리한다.
    class WorldDoubleBufferRoleExchange final
    {
    public:
        static constexpr std::size_t SlotCount = 2;
        static constexpr std::size_t InvalidSlotIndex = SlotCount;

        WorldDoubleBufferRoleExchange() noexcept = default;
        WorldDoubleBufferRoleExchange(const WorldDoubleBufferRoleExchange&) = delete;
        WorldDoubleBufferRoleExchange& operator=(const WorldDoubleBufferRoleExchange&) = delete;

        [[nodiscard]] WorldResult<WorldDoubleBufferWriteClaim, WorldDoubleBufferRoleExchangeError> WaitAcquireWrite(
            std::uint64_t minimumGeneration, std::chrono::milliseconds timeout) noexcept;
        [[nodiscard]] WorldResult<void, WorldDoubleBufferRoleExchangeError> ReleaseWrite(
            const WorldDoubleBufferWriteClaim& claim) noexcept;
        [[nodiscard]] WorldResult<void, WorldDoubleBufferRoleExchangeError> WaitSwap(
            std::uint64_t epoch, std::chrono::milliseconds timeout) noexcept;
        [[nodiscard]] WorldResult<WorldDoubleBufferReadClaim, WorldDoubleBufferRoleExchangeError> WaitAcquireRead(
            std::uint64_t epoch, std::chrono::milliseconds timeout) noexcept;
        [[nodiscard]] WorldResult<void, WorldDoubleBufferRoleExchangeError> ReleaseRead(
            const WorldDoubleBufferReadClaim& claim) noexcept;
        [[nodiscard]] bool MatchesActiveWrite(const WorldDoubleBufferWriteClaim& claim) noexcept;
        [[nodiscard]] bool MatchesActiveRead(const WorldDoubleBufferReadClaim& claim) noexcept;
        void Close() noexcept;

    private:
        std::mutex mutex_;
        std::condition_variable condition_;
        std::size_t writeSlotIndex_ = 0;
        std::size_t readSlotIndex_ = 1;
        std::uint64_t generation_ = 1;
        std::uint64_t publishedEpoch_ = 0;
        bool writeClaimActive_ = false;
        bool readClaimActive_ = false;
        bool readReady_ = false;
        bool swapPending_ = false;
        bool closed_ = false;
    };
} // namespace psnr::world
