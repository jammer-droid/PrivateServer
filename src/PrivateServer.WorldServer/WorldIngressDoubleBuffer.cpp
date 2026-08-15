#include "pch.h"

#include "WorldIngressDoubleBuffer.h"

#include <new>
#include <stdexcept>
#include <utility>

namespace psnr::world
{
    WorldIngressDoubleBuffer::Slot::Slot(const std::size_t eventCapacity)
        : events(eventCapacity)
    {
    }

    WorldResult<std::unique_ptr<WorldIngressDoubleBuffer>> WorldIngressDoubleBuffer::Create(
        const std::size_t eventCapacityPerSlot) noexcept
    {
        if (eventCapacityPerSlot == 0)
        {
            return WorldResult<std::unique_ptr<WorldIngressDoubleBuffer>>::Failure(WorldErrorCode::InvalidArgument);
        }

        try
        {
            std::unique_ptr<WorldIngressDoubleBuffer> buffer{
                new WorldIngressDoubleBuffer(eventCapacityPerSlot),
            };
            return WorldResult<std::unique_ptr<WorldIngressDoubleBuffer>>{std::move(buffer)};
        }
        catch (const std::bad_alloc&)
        {
            return WorldResult<std::unique_ptr<WorldIngressDoubleBuffer>>::Failure(WorldErrorCode::AllocationFailed);
        }
        catch (const std::length_error&)
        {
            return WorldResult<std::unique_ptr<WorldIngressDoubleBuffer>>::Failure(WorldErrorCode::AllocationFailed);
        }
    }

    WorldResult<WorldIngressWriteBatch, WorldDoubleBufferRoleExchangeError> WorldIngressDoubleBuffer::WaitAcquireWrite(
        const std::uint64_t minimumGeneration, const std::chrono::milliseconds timeout) noexcept
    {
        WorldResult<WorldDoubleBufferWriteClaim, WorldDoubleBufferRoleExchangeError> claimResult =
            roleExchange_.WaitAcquireWrite(minimumGeneration, timeout);
        if (claimResult.Failed())
        {
            return WorldResult<WorldIngressWriteBatch, WorldDoubleBufferRoleExchangeError>::Failure(
                claimResult.Error());
        }

        WorldDoubleBufferWriteClaim claim = claimResult.TakeValue();
        Slot& slot = slots_[claim.slotIndex];
        return WorldResult<WorldIngressWriteBatch, WorldDoubleBufferRoleExchangeError>(WorldIngressWriteBatch{
            claim,
            std::span<psnr::runtime::NrToWorldEvent>{slot.events.data() + slot.eventCount,
                                                     slot.events.size() - slot.eventCount},
        });
    }

    WorldResult<void, WorldDoubleBufferRoleExchangeError> WorldIngressDoubleBuffer::CommitWrite(
        const WorldIngressWriteBatch& batch, const std::size_t eventCount) noexcept
    {
        if (!roleExchange_.MatchesActiveWrite(batch.claim))
        {
            return WorldResult<void, WorldDoubleBufferRoleExchangeError>::Failure(
                WorldDoubleBufferRoleExchangeError::InvalidState);
        }

        Slot& slot = slots_[batch.claim.slotIndex];
        if (batch.events.data() != slot.events.data() + slot.eventCount ||
            batch.events.size() != slot.events.size() - slot.eventCount || eventCount > batch.events.size())
        {
            return WorldResult<void, WorldDoubleBufferRoleExchangeError>::Failure(
                WorldDoubleBufferRoleExchangeError::InvalidArgument);
        }

        slot.eventCount += eventCount;
        return roleExchange_.ReleaseWrite(batch.claim);
    }

    WorldResult<void, WorldDoubleBufferRoleExchangeError> WorldIngressDoubleBuffer::WaitSwap(
        const std::uint64_t epoch, const std::chrono::milliseconds timeout) noexcept
    {
        return roleExchange_.WaitSwap(epoch, timeout);
    }

    WorldResult<WorldIngressReadBatch, WorldDoubleBufferRoleExchangeError> WorldIngressDoubleBuffer::WaitAcquireRead(
        const std::uint64_t epoch, const std::chrono::milliseconds timeout) noexcept
    {
        WorldResult<WorldDoubleBufferReadClaim, WorldDoubleBufferRoleExchangeError> claimResult =
            roleExchange_.WaitAcquireRead(epoch, timeout);
        if (claimResult.Failed())
        {
            return WorldResult<WorldIngressReadBatch, WorldDoubleBufferRoleExchangeError>::Failure(claimResult.Error());
        }

        WorldDoubleBufferReadClaim claim = claimResult.TakeValue();
        const Slot& slot = slots_[claim.slotIndex];
        return WorldResult<WorldIngressReadBatch, WorldDoubleBufferRoleExchangeError>(WorldIngressReadBatch{
            claim,
            claim.epoch,
            std::span<const psnr::runtime::NrToWorldEvent>{slot.events.data(), slot.eventCount},
        });
    }

    WorldResult<void, WorldDoubleBufferRoleExchangeError> WorldIngressDoubleBuffer::ReleaseRead(
        const WorldIngressReadBatch& batch) noexcept
    {
        if (!roleExchange_.MatchesActiveRead(batch.claim) || batch.epoch != batch.claim.epoch)
        {
            return WorldResult<void, WorldDoubleBufferRoleExchangeError>::Failure(
                WorldDoubleBufferRoleExchangeError::InvalidState);
        }

        Slot& slot = slots_[batch.claim.slotIndex];
        if (batch.events.data() != slot.events.data() || batch.events.size() != slot.eventCount)
        {
            return WorldResult<void, WorldDoubleBufferRoleExchangeError>::Failure(
                WorldDoubleBufferRoleExchangeError::InvalidArgument);
        }

        for (std::size_t index = 0; index < slot.eventCount; ++index)
        {
            slot.events[index] = psnr::runtime::NrToWorldEvent{};
        }
        slot.eventCount = 0;
        return roleExchange_.ReleaseRead(batch.claim);
    }

    std::size_t WorldIngressDoubleBuffer::EventCapacityPerSlot() const noexcept
    {
        return slots_[0].events.size();
    }

    void WorldIngressDoubleBuffer::Close() noexcept
    {
        roleExchange_.Close();
    }

    WorldIngressDoubleBuffer::WorldIngressDoubleBuffer(const std::size_t eventCapacityPerSlot)
        : slots_{Slot{eventCapacityPerSlot}, Slot{eventCapacityPerSlot}}
    {
    }
} // namespace psnr::world
