#include "pch.h"

#include "WorldDoubleBufferRoleExchange.h"

#include <limits>
#include <utility>

namespace psnr::world
{
    WorldResult<WorldDoubleBufferWriteClaim, WorldDoubleBufferRoleExchangeError> WorldDoubleBufferRoleExchange::
        WaitAcquireWrite(const std::uint64_t minimumGeneration, const std::chrono::milliseconds timeout) noexcept
    {
        if (timeout.count() < 0)
        {
            return WorldResult<WorldDoubleBufferWriteClaim, WorldDoubleBufferRoleExchangeError>::Failure(
                WorldDoubleBufferRoleExchangeError::InvalidArgument);
        }

        std::unique_lock<std::mutex> lock{mutex_};
        const bool ready = condition_.wait_for(
            lock, timeout, [this, minimumGeneration]() noexcept
            { return closed_ || (!swapPending_ && !writeClaimActive_ && generation_ >= minimumGeneration); });
        if (!ready)
        {
            return WorldResult<WorldDoubleBufferWriteClaim, WorldDoubleBufferRoleExchangeError>::Failure(
                WorldDoubleBufferRoleExchangeError::TimedOut);
        }
        if (closed_)
        {
            return WorldResult<WorldDoubleBufferWriteClaim, WorldDoubleBufferRoleExchangeError>::Failure(
                WorldDoubleBufferRoleExchangeError::Closed);
        }

        writeClaimActive_ = true;
        return WorldResult<WorldDoubleBufferWriteClaim, WorldDoubleBufferRoleExchangeError>(
            WorldDoubleBufferWriteClaim{writeSlotIndex_, generation_});
    }

    WorldResult<void, WorldDoubleBufferRoleExchangeError> WorldDoubleBufferRoleExchange::ReleaseWrite(
        const WorldDoubleBufferWriteClaim& claim) noexcept
    {
        std::lock_guard<std::mutex> lock{mutex_};
        if (!writeClaimActive_ || claim.slotIndex != writeSlotIndex_ || claim.generation != generation_)
        {
            return WorldResult<void, WorldDoubleBufferRoleExchangeError>::Failure(
                WorldDoubleBufferRoleExchangeError::InvalidState);
        }

        writeClaimActive_ = false;
        condition_.notify_all();
        return WorldResult<void, WorldDoubleBufferRoleExchangeError>::Success();
    }

    WorldResult<void, WorldDoubleBufferRoleExchangeError> WorldDoubleBufferRoleExchange::WaitSwap(
        const std::uint64_t epoch, const std::chrono::milliseconds timeout) noexcept
    {
        if (epoch == 0 || timeout.count() < 0)
        {
            return WorldResult<void, WorldDoubleBufferRoleExchangeError>::Failure(
                WorldDoubleBufferRoleExchangeError::InvalidArgument);
        }

        std::unique_lock<std::mutex> lock{mutex_};
        if (closed_)
        {
            return WorldResult<void, WorldDoubleBufferRoleExchangeError>::Failure(
                WorldDoubleBufferRoleExchangeError::Closed);
        }
        if (epoch <= publishedEpoch_ || generation_ == std::numeric_limits<std::uint64_t>::max() || swapPending_)
        {
            return WorldResult<void, WorldDoubleBufferRoleExchangeError>::Failure(
                WorldDoubleBufferRoleExchangeError::InvalidState);
        }

        swapPending_ = true;
        const bool ready =
            condition_.wait_for(lock, timeout, [this]() noexcept
                                { return closed_ || (!writeClaimActive_ && !readClaimActive_ && !readReady_); });
        if (!ready)
        {
            swapPending_ = false;
            condition_.notify_all();
            return WorldResult<void, WorldDoubleBufferRoleExchangeError>::Failure(
                WorldDoubleBufferRoleExchangeError::TimedOut);
        }
        if (closed_)
        {
            swapPending_ = false;
            return WorldResult<void, WorldDoubleBufferRoleExchangeError>::Failure(
                WorldDoubleBufferRoleExchangeError::Closed);
        }

        std::swap(writeSlotIndex_, readSlotIndex_);
        ++generation_;
        publishedEpoch_ = epoch;
        readReady_ = true;
        swapPending_ = false;
        condition_.notify_all();
        return WorldResult<void, WorldDoubleBufferRoleExchangeError>::Success();
    }

    WorldResult<WorldDoubleBufferReadClaim, WorldDoubleBufferRoleExchangeError> WorldDoubleBufferRoleExchange::
        WaitAcquireRead(const std::uint64_t epoch, const std::chrono::milliseconds timeout) noexcept
    {
        if (epoch == 0 || timeout.count() < 0)
        {
            return WorldResult<WorldDoubleBufferReadClaim, WorldDoubleBufferRoleExchangeError>::Failure(
                WorldDoubleBufferRoleExchangeError::InvalidArgument);
        }

        std::unique_lock<std::mutex> lock{mutex_};
        const bool ready = condition_.wait_for(lock, timeout, [this]() noexcept { return closed_ || readReady_; });
        if (!ready)
        {
            return WorldResult<WorldDoubleBufferReadClaim, WorldDoubleBufferRoleExchangeError>::Failure(
                WorldDoubleBufferRoleExchangeError::TimedOut);
        }
        if (closed_)
        {
            return WorldResult<WorldDoubleBufferReadClaim, WorldDoubleBufferRoleExchangeError>::Failure(
                WorldDoubleBufferRoleExchangeError::Closed);
        }
        if (readClaimActive_ || publishedEpoch_ != epoch)
        {
            return WorldResult<WorldDoubleBufferReadClaim, WorldDoubleBufferRoleExchangeError>::Failure(
                WorldDoubleBufferRoleExchangeError::InvalidState);
        }

        readReady_ = false;
        readClaimActive_ = true;
        return WorldResult<WorldDoubleBufferReadClaim, WorldDoubleBufferRoleExchangeError>(
            WorldDoubleBufferReadClaim{readSlotIndex_, generation_, publishedEpoch_});
    }

    WorldResult<void, WorldDoubleBufferRoleExchangeError> WorldDoubleBufferRoleExchange::ReleaseRead(
        const WorldDoubleBufferReadClaim& claim) noexcept
    {
        std::lock_guard<std::mutex> lock{mutex_};
        if (!readClaimActive_ || claim.slotIndex != readSlotIndex_ || claim.generation != generation_ ||
            claim.epoch != publishedEpoch_)
        {
            return WorldResult<void, WorldDoubleBufferRoleExchangeError>::Failure(
                WorldDoubleBufferRoleExchangeError::InvalidState);
        }

        readClaimActive_ = false;
        condition_.notify_all();
        return WorldResult<void, WorldDoubleBufferRoleExchangeError>::Success();
    }

    bool WorldDoubleBufferRoleExchange::MatchesActiveWrite(const WorldDoubleBufferWriteClaim& claim) noexcept
    {
        std::lock_guard<std::mutex> lock{mutex_};
        return writeClaimActive_ && claim.slotIndex == writeSlotIndex_ && claim.generation == generation_;
    }

    bool WorldDoubleBufferRoleExchange::MatchesActiveRead(const WorldDoubleBufferReadClaim& claim) noexcept
    {
        std::lock_guard<std::mutex> lock{mutex_};
        return readClaimActive_ && claim.slotIndex == readSlotIndex_ && claim.generation == generation_ &&
               claim.epoch == publishedEpoch_;
    }

    void WorldDoubleBufferRoleExchange::Close() noexcept
    {
        std::lock_guard<std::mutex> lock{mutex_};
        closed_ = true;
        condition_.notify_all();
    }
} // namespace psnr::world
