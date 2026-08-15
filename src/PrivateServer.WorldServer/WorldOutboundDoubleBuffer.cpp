#include "pch.h"

#include "WorldOutboundDoubleBuffer.h"

#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace psnr::world
{
    namespace
    {
        [[nodiscard]] bool IsValidCapacity(const WorldOutboundBatchCapacity& capacity) noexcept
        {
            constexpr std::size_t MaximumOffset = std::numeric_limits<std::uint32_t>::max();
            return capacity.recordCount > 0 && capacity.recipientCount > 0 && capacity.payloadByteCount > 0 &&
                   capacity.recordCount <= MaximumOffset && capacity.recipientCount <= MaximumOffset &&
                   capacity.payloadByteCount <= MaximumOffset;
        }

        [[nodiscard]] bool IsValidSlotCount(const WorldOutboundBufferSlotCount slotCount) noexcept
        {
            return slotCount == WorldOutboundBufferSlotCount::Double ||
                   slotCount == WorldOutboundBufferSlotCount::Triple;
        }
    } // namespace

    WorldOutboundDoubleBuffer::Slot::Slot(const WorldOutboundBatchCapacity& capacity)
        : records(capacity.recordCount)
        , recipients(capacity.recipientCount)
        , payloadBytes(capacity.payloadByteCount)
    {
    }

    WorldResult<std::unique_ptr<WorldOutboundDoubleBuffer>> WorldOutboundDoubleBuffer::Create(
        const WorldOutboundBatchCapacity& capacity, const WorldOutboundBufferSlotCount slotCount) noexcept
    {
        if (!IsValidCapacity(capacity) || !IsValidSlotCount(slotCount))
        {
            return WorldResult<std::unique_ptr<WorldOutboundDoubleBuffer>>::Failure(WorldErrorCode::InvalidArgument);
        }

        try
        {
            std::unique_ptr<WorldOutboundDoubleBuffer> buffer{
                new WorldOutboundDoubleBuffer(capacity, slotCount),
            };
            return WorldResult<std::unique_ptr<WorldOutboundDoubleBuffer>>{std::move(buffer)};
        }
        catch (const std::bad_alloc&)
        {
            return WorldResult<std::unique_ptr<WorldOutboundDoubleBuffer>>::Failure(WorldErrorCode::AllocationFailed);
        }
        catch (const std::length_error&)
        {
            return WorldResult<std::unique_ptr<WorldOutboundDoubleBuffer>>::Failure(WorldErrorCode::AllocationFailed);
        }
    }

    WorldOutboundDoubleBufferExchangeResult WorldOutboundDoubleBuffer::WaitPrepareWrite(
        const std::chrono::milliseconds timeout) noexcept
    {
        if (timeout.count() < 0)
        {
            return WorldOutboundDoubleBufferExchangeResult::InvalidArgument;
        }

        std::unique_lock<std::mutex> lock{mutex_};
        const bool ready = condition_.wait_for(lock, timeout,
                                               [this]() noexcept
                                               {
                                                   return closed_.load(std::memory_order_acquire) ||
                                                          writesFinished_.load(std::memory_order_acquire) ||
                                                          writeSlotActive_ ||
                                                          FindEmptySlotIndexLocked() != InvalidSlotIndex;
                                               });
        if (!ready)
        {
            return timeout.count() == 0 ? WorldOutboundDoubleBufferExchangeResult::Busy
                                        : WorldOutboundDoubleBufferExchangeResult::TimedOut;
        }
        if (closed_.load(std::memory_order_acquire) || writesFinished_.load(std::memory_order_acquire))
        {
            return WorldOutboundDoubleBufferExchangeResult::InvalidState;
        }
        if (writeSlotActive_)
        {
            return WorldOutboundDoubleBufferExchangeResult::Exchanged;
        }

        const std::size_t nextWriteSlotIndex = FindEmptySlotIndexLocked();
        if (nextWriteSlotIndex == InvalidSlotIndex)
        {
            return WorldOutboundDoubleBufferExchangeResult::InvalidState;
        }
        slots_[nextWriteSlotIndex].state = SlotState::Writing;
        writeSlotIndex_ = nextWriteSlotIndex;
        writeSlotActive_ = true;
        return WorldOutboundDoubleBufferExchangeResult::Exchanged;
    }

    WorldOutboundDoubleBufferExchangeResult WorldOutboundDoubleBuffer::BeginWriteBatch(
        const std::uint64_t epoch, const std::uint32_t firstServerTick, const std::uint32_t lastServerTick) noexcept
    {
        if (epoch == 0 || firstServerTick == 0 || firstServerTick > lastServerTick)
        {
            return WorldOutboundDoubleBufferExchangeResult::InvalidArgument;
        }
        if (closed_.load(std::memory_order_acquire) || writesFinished_.load(std::memory_order_acquire))
        {
            return WorldOutboundDoubleBufferExchangeResult::InvalidState;
        }

        std::lock_guard<std::mutex> lock{mutex_};
        if (epoch <= lastSealedEpoch_)
        {
            return WorldOutboundDoubleBufferExchangeResult::InvalidState;
        }
        if (!writeSlotActive_)
        {
            const std::size_t nextWriteSlotIndex = FindEmptySlotIndexLocked();
            if (nextWriteSlotIndex == InvalidSlotIndex)
            {
                return WorldOutboundDoubleBufferExchangeResult::Busy;
            }
            slots_[nextWriteSlotIndex].state = SlotState::Writing;
            writeSlotIndex_ = nextWriteSlotIndex;
            writeSlotActive_ = true;
        }
        if (writeSlotIndex_ >= slots_.size())
        {
            return WorldOutboundDoubleBufferExchangeResult::InvalidState;
        }
        Slot& slot = slots_[writeSlotIndex_];
        if (slot.state != SlotState::Writing || slot.epoch != 0 || slot.usage != WorldOutboundBatchUsage{} ||
            slot.replicationRecipientCount != 0)
        {
            return WorldOutboundDoubleBufferExchangeResult::InvalidState;
        }

        slot.epoch = epoch;
        slot.firstServerTick = firstServerTick;
        slot.lastServerTick = lastServerTick;
        return WorldOutboundDoubleBufferExchangeResult::Exchanged;
    }

    WorldOutboundAppendResult WorldOutboundDoubleBuffer::TryAppend(
        const psnr::core::NrPacketType packetType,
        const std::span<const psnr::runtime::NrSessionSendChannel> recipients,
        const std::span<const std::byte> payload) noexcept
    {
        if (closed_.load(std::memory_order_acquire) || writesFinished_.load(std::memory_order_acquire))
        {
            return RecordAppendFailure(WorldOutboundWriteOperation::Append, WorldOutboundAppendResult::InvalidState,
                                       packetType, 0, recipients.size(), payload.size());
        }
        if (packetType.value == 0 || recipients.empty())
        {
            return RecordAppendFailure(WorldOutboundWriteOperation::Append, WorldOutboundAppendResult::InvalidArgument,
                                       packetType, 0, recipients.size(), payload.size());
        }

        Slot& slot = slots_[writeSlotIndex_];
        if (slot.state != SlotState::Writing)
        {
            return RecordAppendFailure(WorldOutboundWriteOperation::Append, WorldOutboundAppendResult::InvalidState,
                                       packetType, 0, recipients.size(), payload.size());
        }
        if (slot.usage.recordCount == slot.records.size() ||
            recipients.size() > slot.recipients.size() - slot.usage.recipientCount ||
            payload.size() > slot.payloadBytes.size() - slot.usage.payloadByteCount)
        {
            return RecordAppendFailure(WorldOutboundWriteOperation::Append, WorldOutboundAppendResult::CapacityExceeded,
                                       packetType, 0, recipients.size(), payload.size());
        }

        const std::size_t recipientOffset = slot.usage.recipientCount;
        const std::size_t payloadOffset = slot.usage.payloadByteCount;
        for (std::size_t index = 0; index < recipients.size(); ++index)
        {
            slot.recipients[recipientOffset + index] = recipients[index];
        }
        std::copy(payload.begin(), payload.end(), slot.payloadBytes.begin() + payloadOffset);

        slot.records[slot.usage.recordCount] = WorldOutboundRecord{
            packetType,
            static_cast<std::uint32_t>(recipientOffset),
            static_cast<std::uint32_t>(recipients.size()),
            static_cast<std::uint32_t>(payloadOffset),
            static_cast<std::uint32_t>(payload.size()),
            WorldOutboundRecordMetadata{},
        };
        ++slot.usage.recordCount;
        slot.usage.recipientCount += recipients.size();
        slot.usage.payloadByteCount += payload.size();
        return WorldOutboundAppendResult::Appended;
    }

    WorldOutboundAppendResult WorldOutboundDoubleBuffer::TryAppendEncoded(
        const psnr::core::NrPacketType packetType,
        const std::span<const psnr::runtime::NrSessionSendChannel> recipients, const std::size_t payloadSize,
        const WorldOutboundPayloadEncoder encoder, void* const encoderContext,
        const WorldOutboundRecordMetadata metadata) noexcept
    {
        if (closed_.load(std::memory_order_acquire) || writesFinished_.load(std::memory_order_acquire))
        {
            return RecordAppendFailure(WorldOutboundWriteOperation::AppendEncoded,
                                       WorldOutboundAppendResult::InvalidState, packetType, 0, recipients.size(),
                                       payloadSize, metadata);
        }
        if (packetType.value == 0 || recipients.empty() || encoder == nullptr)
        {
            return RecordAppendFailure(WorldOutboundWriteOperation::AppendEncoded,
                                       WorldOutboundAppendResult::InvalidArgument, packetType, 0, recipients.size(),
                                       payloadSize, metadata);
        }

        Slot& slot = slots_[writeSlotIndex_];
        if (slot.state != SlotState::Writing)
        {
            return RecordAppendFailure(WorldOutboundWriteOperation::AppendEncoded,
                                       WorldOutboundAppendResult::InvalidState, packetType, 0, recipients.size(),
                                       payloadSize, metadata);
        }
        if (metadata.kind != WorldOutboundRecordKind::Generic &&
            (metadata.entityCount == 0 || metadata.replicationRecipientIndex >= slot.replicationRecipientCount))
        {
            return RecordAppendFailure(WorldOutboundWriteOperation::AppendEncoded,
                                       WorldOutboundAppendResult::InvalidArgument, packetType, 0, recipients.size(),
                                       payloadSize, metadata);
        }
        if (slot.usage.recordCount == slot.records.size() ||
            recipients.size() > slot.recipients.size() - slot.usage.recipientCount ||
            payloadSize > slot.payloadBytes.size() - slot.usage.payloadByteCount)
        {
            return RecordAppendFailure(WorldOutboundWriteOperation::AppendEncoded,
                                       WorldOutboundAppendResult::CapacityExceeded, packetType, 0, recipients.size(),
                                       payloadSize, metadata);
        }

        const std::size_t recipientOffset = slot.usage.recipientCount;
        const std::size_t payloadOffset = slot.usage.payloadByteCount;
        const std::span<std::byte> payload{
            slot.payloadBytes.data() + payloadOffset,
            payloadSize,
        };
        if (!encoder(encoderContext, payload))
        {
            return RecordAppendFailure(WorldOutboundWriteOperation::AppendEncoded,
                                       WorldOutboundAppendResult::EncodingFailed, packetType, 0, recipients.size(),
                                       payloadSize, metadata);
        }

        for (std::size_t index = 0; index < recipients.size(); ++index)
        {
            slot.recipients[recipientOffset + index] = recipients[index];
        }
        slot.records[slot.usage.recordCount] = WorldOutboundRecord{
            packetType,
            static_cast<std::uint32_t>(recipientOffset),
            static_cast<std::uint32_t>(recipients.size()),
            static_cast<std::uint32_t>(payloadOffset),
            static_cast<std::uint32_t>(payloadSize),
            metadata,
        };
        ++slot.usage.recordCount;
        slot.usage.recipientCount += recipients.size();
        slot.usage.payloadByteCount += payloadSize;
        return WorldOutboundAppendResult::Appended;
    }

    WorldOutboundAppendResult WorldOutboundDoubleBuffer::ReserveReplicationRecipients(
        const std::uint32_t recipientCount, std::uint32_t* const outFirstRecipientIndex) noexcept
    {
        if (closed_.load(std::memory_order_acquire) || writesFinished_.load(std::memory_order_acquire))
        {
            return RecordAppendFailure(WorldOutboundWriteOperation::ReserveReplicationRecipients,
                                       WorldOutboundAppendResult::InvalidState, {}, 0, recipientCount, 0);
        }
        Slot& slot = slots_[writeSlotIndex_];
        if (slot.state != SlotState::Writing || slot.epoch == 0 || slot.lastServerTick == 0)
        {
            return RecordAppendFailure(WorldOutboundWriteOperation::ReserveReplicationRecipients,
                                       WorldOutboundAppendResult::InvalidState, {}, slot.lastServerTick, recipientCount,
                                       0);
        }
        if (recipientCount == 0 || outFirstRecipientIndex == nullptr ||
            recipientCount > std::numeric_limits<std::uint32_t>::max() - slot.replicationRecipientCount)
        {
            return RecordAppendFailure(WorldOutboundWriteOperation::ReserveReplicationRecipients,
                                       WorldOutboundAppendResult::InvalidArgument, {}, slot.lastServerTick,
                                       recipientCount, 0);
        }
        *outFirstRecipientIndex = slot.replicationRecipientCount;
        slot.replicationRecipientCount += recipientCount;
        return WorldOutboundAppendResult::Appended;
    }

    WorldOutboundDoubleBufferExchangeResult WorldOutboundDoubleBuffer::SealWrite(const std::uint64_t epoch) noexcept
    {
        if (epoch == 0)
        {
            return WorldOutboundDoubleBufferExchangeResult::InvalidArgument;
        }
        if (closed_.load(std::memory_order_acquire) || writesFinished_.load(std::memory_order_acquire))
        {
            return WorldOutboundDoubleBufferExchangeResult::InvalidState;
        }

        std::lock_guard<std::mutex> lock{mutex_};
        if (writeSlotIndex_ >= slots_.size())
        {
            return WorldOutboundDoubleBufferExchangeResult::InvalidState;
        }
        Slot& writeSlot = slots_[writeSlotIndex_];
        if (!writeSlotActive_ || writeSlot.state != SlotState::Writing || epoch <= lastSealedEpoch_ ||
            (writeSlot.epoch != 0 && writeSlot.epoch != epoch))
        {
            return WorldOutboundDoubleBufferExchangeResult::InvalidState;
        }
        if (writeSlot.usage.recordCount == 0)
        {
            writeSlot.epoch = 0;
            writeSlot.firstServerTick = 0;
            writeSlot.lastServerTick = 0;
            return WorldOutboundDoubleBufferExchangeResult::Empty;
        }

        writeSlot.epoch = epoch;
        writeSlot.state = SlotState::Sealed;
        lastSealedEpoch_ = epoch;
        writeSlotActive_ = false;
        EnqueueReadySlotLocked(writeSlotIndex_);
        condition_.notify_all();
        return WorldOutboundDoubleBufferExchangeResult::Exchanged;
    }

    WorldOutboundDoubleBufferExchangeResult WorldOutboundDoubleBuffer::WaitAcquireRead(
        const std::chrono::milliseconds timeout, WorldOutboundReadBatch* const outBatch) noexcept
    {
        if (timeout.count() < 0 || outBatch == nullptr)
        {
            return WorldOutboundDoubleBufferExchangeResult::InvalidArgument;
        }

        std::unique_lock<std::mutex> lock{mutex_};
        const bool ready = condition_.wait_for(
            lock, timeout,
            [this]() noexcept
            {
                return closed_.load(std::memory_order_acquire) ||
                       (!readClaimActive_ && (readySlotCount_ != 0 || writesFinished_.load(std::memory_order_acquire)));
            });
        if (!ready)
        {
            return WorldOutboundDoubleBufferExchangeResult::TimedOut;
        }
        if (closed_.load(std::memory_order_acquire))
        {
            return WorldOutboundDoubleBufferExchangeResult::InvalidState;
        }
        if (readySlotCount_ == 0 && writesFinished_.load(std::memory_order_acquire))
        {
            return WorldOutboundDoubleBufferExchangeResult::Empty;
        }

        const std::size_t readSlotIndex = DequeueReadySlotLocked();
        if (readSlotIndex >= slots_.size())
        {
            return WorldOutboundDoubleBufferExchangeResult::InvalidState;
        }
        Slot& readSlot = slots_[readSlotIndex];
        if (readSlot.state != SlotState::Sealed || readSlot.epoch == 0)
        {
            return WorldOutboundDoubleBufferExchangeResult::InvalidState;
        }

        readSlotIndex_ = readSlotIndex;
        readClaimActive_ = true;
        readSlot.state = SlotState::Reading;
        *outBatch = WorldOutboundReadBatch{
            readSlot.epoch,
            readSlot.firstServerTick,
            readSlot.lastServerTick,
            std::span<const WorldOutboundRecord>{readSlot.records.data(), readSlot.usage.recordCount},
            std::span<const psnr::runtime::NrSessionSendChannel>{readSlot.recipients.data(),
                                                                 readSlot.usage.recipientCount},
            std::span<const std::byte>{readSlot.payloadBytes.data(), readSlot.usage.payloadByteCount},
            readSlot.lastServerTick,
            readSlot.replicationRecipientCount,
        };
        return WorldOutboundDoubleBufferExchangeResult::Exchanged;
    }

    WorldOutboundDoubleBufferExchangeResult WorldOutboundDoubleBuffer::ReleaseRead(const std::uint64_t epoch) noexcept
    {
        if (epoch == 0)
        {
            return WorldOutboundDoubleBufferExchangeResult::InvalidArgument;
        }

        std::lock_guard<std::mutex> lock{mutex_};
        if (!readClaimActive_ || readSlotIndex_ >= slots_.size())
        {
            return WorldOutboundDoubleBufferExchangeResult::InvalidState;
        }

        Slot& readSlot = slots_[readSlotIndex_];
        if (readSlot.state != SlotState::Reading || readSlot.epoch != epoch)
        {
            return WorldOutboundDoubleBufferExchangeResult::InvalidState;
        }
        for (std::size_t index = 0; index < readSlot.usage.recordCount; ++index)
        {
            readSlot.records[index] = WorldOutboundRecord{};
        }
        for (std::size_t index = 0; index < readSlot.usage.recipientCount; ++index)
        {
            readSlot.recipients[index] = psnr::runtime::NrSessionSendChannel{};
        }
        readSlot.usage = WorldOutboundBatchUsage{};
        readSlot.firstServerTick = 0;
        readSlot.lastServerTick = 0;
        readSlot.replicationRecipientCount = 0;

        readSlot.epoch = 0;
        readSlot.state = SlotState::Empty;
        readClaimActive_ = false;
        readSlotIndex_ = InvalidSlotIndex;
        condition_.notify_all();
        return WorldOutboundDoubleBufferExchangeResult::Exchanged;
    }

    WorldOutboundBatchCapacity WorldOutboundDoubleBuffer::CapacityPerSlot() const noexcept
    {
        return capacity_;
    }

    std::size_t WorldOutboundDoubleBuffer::ConfiguredSlotCount() const noexcept
    {
        return slots_.size();
    }

    WorldOutboundBatchUsage WorldOutboundDoubleBuffer::WritableUsage() const noexcept
    {
        return slots_[writeSlotIndex_].usage;
    }

    WorldOutboundAppendFailure WorldOutboundDoubleBuffer::LastAppendFailure() const noexcept
    {
        return lastAppendFailure_;
    }

    void WorldOutboundDoubleBuffer::FinishWrites() noexcept
    {
        {
            std::lock_guard<std::mutex> lock{mutex_};
            writesFinished_.store(true, std::memory_order_release);
        }
        condition_.notify_all();
    }

    void WorldOutboundDoubleBuffer::Close() noexcept
    {
        {
            std::lock_guard<std::mutex> lock{mutex_};
            closed_.store(true, std::memory_order_release);
        }
        condition_.notify_all();
    }

    WorldOutboundAppendResult WorldOutboundDoubleBuffer::RecordAppendFailure(
        const WorldOutboundWriteOperation operation, const WorldOutboundAppendResult result,
        const psnr::core::NrPacketType packetType, const std::uint32_t serverTick,
        const std::size_t requestedRecipientCount, const std::size_t requestedPayloadByteCount,
        const WorldOutboundRecordMetadata metadata) noexcept
    {
        if (!lastAppendFailure_.Present())
        {
            const Slot& slot = slots_[writeSlotIndex_];
            lastAppendFailure_ = WorldOutboundAppendFailure{
                operation,
                result,
                packetType,
                serverTick,
                slot.lastServerTick,
                slot.replicationRecipientCount,
                metadata.replicationRecipientIndex,
                metadata.entityCount,
                requestedRecipientCount,
                requestedPayloadByteCount,
                slot.usage,
            };
        }
        return result;
    }

    WorldOutboundDoubleBuffer::WorldOutboundDoubleBuffer(const WorldOutboundBatchCapacity& capacity,
                                                         const WorldOutboundBufferSlotCount slotCount)
        : capacity_(capacity)
    {
        const std::size_t configuredSlotCount = static_cast<std::size_t>(slotCount);
        slots_.reserve(configuredSlotCount);
        for (std::size_t index = 0; index < configuredSlotCount; ++index)
        {
            slots_.emplace_back(capacity);
        }
        writeSlotIndex_ = 0;
        slots_[writeSlotIndex_].state = SlotState::Writing;
        writeSlotActive_ = true;
    }

    std::size_t WorldOutboundDoubleBuffer::FindEmptySlotIndexLocked() const noexcept
    {
        for (std::size_t index = 0; index < slots_.size(); ++index)
        {
            if (slots_[index].state == SlotState::Empty)
            {
                return index;
            }
        }
        return InvalidSlotIndex;
    }

    void WorldOutboundDoubleBuffer::EnqueueReadySlotLocked(const std::size_t slotIndex) noexcept
    {
        const std::size_t readyTailIndex = (readyHeadIndex_ + readySlotCount_) % readySlotIndices_.size();
        readySlotIndices_[readyTailIndex] = slotIndex;
        ++readySlotCount_;
    }

    std::size_t WorldOutboundDoubleBuffer::DequeueReadySlotLocked() noexcept
    {
        if (readySlotCount_ == 0)
        {
            return InvalidSlotIndex;
        }

        const std::size_t slotIndex = readySlotIndices_[readyHeadIndex_];
        readySlotIndices_[readyHeadIndex_] = InvalidSlotIndex;
        readyHeadIndex_ = (readyHeadIndex_ + 1) % readySlotIndices_.size();
        --readySlotCount_;
        return slotIndex;
    }
} // namespace psnr::world
