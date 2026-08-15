#pragma once

#include "WorldResult.h"

#include <PrivateServer/NetworkRuntime/NrGateway.h>

#include <atomic>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

namespace psnr::world
{
    enum class WorldOutboundRecordKind : std::uint8_t
    {
        Generic = 0,
        ReplicationSpawn,
        ReplicationStateBatch,
        ReplicationRemove,
    };

    struct WorldOutboundRecordMetadata final
    {
        WorldOutboundRecordKind kind = WorldOutboundRecordKind::Generic;
        std::uint32_t replicationRecipientIndex = 0;
        std::uint32_t entityCount = 0;

        [[nodiscard]] friend bool operator==(const WorldOutboundRecordMetadata& left,
                                             const WorldOutboundRecordMetadata& right) noexcept = default;
    };

    struct WorldOutboundBatchCapacity final
    {
        std::size_t recordCount = 0;
        std::size_t recipientCount = 0;
        std::size_t payloadByteCount = 0;

        [[nodiscard]] friend bool operator==(const WorldOutboundBatchCapacity& left,
                                             const WorldOutboundBatchCapacity& right) noexcept = default;
    };

    struct WorldOutboundBatchUsage final
    {
        std::size_t recordCount = 0;
        std::size_t recipientCount = 0;
        std::size_t payloadByteCount = 0;

        [[nodiscard]] friend bool operator==(const WorldOutboundBatchUsage& left,
                                             const WorldOutboundBatchUsage& right) noexcept = default;
    };

    struct WorldOutboundRecord final
    {
        psnr::core::NrPacketType packetType{};
        std::uint32_t recipientOffset = 0;
        std::uint32_t recipientCount = 0;
        std::uint32_t payloadOffset = 0;
        std::uint32_t payloadSize = 0;
        WorldOutboundRecordMetadata metadata{};

        [[nodiscard]] friend bool operator==(const WorldOutboundRecord& left,
                                             const WorldOutboundRecord& right) noexcept = default;
    };

    struct WorldOutboundReadBatch final
    {
        std::uint64_t epoch = 0;
        std::uint32_t firstServerTick = 0;
        std::uint32_t lastServerTick = 0;
        std::span<const WorldOutboundRecord> records{};
        std::span<const psnr::runtime::NrSessionSendChannel> recipients{};
        std::span<const std::byte> payloadBytes{};
        std::uint32_t replicationServerTick = 0;
        std::uint32_t replicationRecipientCount = 0;
    };

    enum class WorldOutboundAppendResult : std::uint8_t
    {
        Appended = 0,
        InvalidArgument,
        InvalidState,
        CapacityExceeded,
        EncodingFailed,
    };

    enum class WorldOutboundWriteOperation : std::uint8_t
    {
        None = 0,
        Append,
        AppendEncoded,
        ReserveReplicationRecipients,
    };

    struct WorldOutboundAppendFailure final
    {
        WorldOutboundWriteOperation operation = WorldOutboundWriteOperation::None;
        WorldOutboundAppendResult result = WorldOutboundAppendResult::Appended;
        psnr::core::NrPacketType packetType{};
        std::uint32_t serverTick = 0;
        std::uint32_t existingReplicationServerTick = 0;
        std::uint32_t existingReplicationRecipientCount = 0;
        std::uint32_t replicationRecipientIndex = 0;
        std::uint32_t entityCount = 0;
        std::size_t requestedRecipientCount = 0;
        std::size_t requestedPayloadByteCount = 0;
        WorldOutboundBatchUsage usage{};

        [[nodiscard]] bool Present() const noexcept
        {
            return operation != WorldOutboundWriteOperation::None;
        }

        [[nodiscard]] friend bool operator==(const WorldOutboundAppendFailure& left,
                                             const WorldOutboundAppendFailure& right) noexcept = default;
    };

    using WorldOutboundPayloadEncoder = bool (*)(void* context, std::span<std::byte> output) noexcept;

    enum class WorldOutboundDoubleBufferExchangeResult : std::uint8_t
    {
        Exchanged = 0,
        Empty,
        InvalidArgument,
        InvalidState,
        Busy,
        TimedOut,
    };

    enum class WorldOutboundBufferSlotCount : std::uint8_t
    {
        Double = 2,
        Triple = 3,
    };

    // coordinator만 write slot에 append/seal하고 publisher만 sealed read slot을 FIFO로 acquire/release한다.
    // 2/3개 storage는 startup에 고정 크기로 할당하며 append 중 동적 확장이나 fallback은 허용하지 않는다.
    class WorldOutboundDoubleBuffer final
    {
    public:
        WorldOutboundDoubleBuffer(const WorldOutboundDoubleBuffer&) = delete;
        WorldOutboundDoubleBuffer& operator=(const WorldOutboundDoubleBuffer&) = delete;

        [[nodiscard]] static WorldResult<std::unique_ptr<WorldOutboundDoubleBuffer>> Create(
            const WorldOutboundBatchCapacity& capacity,
            WorldOutboundBufferSlotCount slotCount = WorldOutboundBufferSlotCount::Double) noexcept;

        [[nodiscard]] WorldOutboundDoubleBufferExchangeResult WaitPrepareWrite(
            std::chrono::milliseconds timeout) noexcept;
        // Coordinator가 write 전에 batch 수명과 simulation tick 범위를 확정한다.
        [[nodiscard]] WorldOutboundDoubleBufferExchangeResult BeginWriteBatch(std::uint64_t epoch,
                                                                              std::uint32_t firstServerTick,
                                                                              std::uint32_t lastServerTick) noexcept;
        [[nodiscard]] WorldOutboundAppendResult TryAppend(
            psnr::core::NrPacketType packetType, std::span<const psnr::runtime::NrSessionSendChannel> recipients,
            std::span<const std::byte> payload) noexcept;
        [[nodiscard]] WorldOutboundAppendResult TryAppendEncoded(
            psnr::core::NrPacketType packetType, std::span<const psnr::runtime::NrSessionSendChannel> recipients,
            std::size_t payloadSize, WorldOutboundPayloadEncoder encoder, void* encoderContext,
            WorldOutboundRecordMetadata metadata = {}) noexcept;
        [[nodiscard]] WorldOutboundAppendResult ReserveReplicationRecipients(
            std::uint32_t recipientCount, std::uint32_t* outFirstRecipientIndex) noexcept;
        [[nodiscard]] WorldOutboundDoubleBufferExchangeResult SealWrite(std::uint64_t epoch) noexcept;
        [[nodiscard]] WorldOutboundDoubleBufferExchangeResult WaitAcquireRead(
            std::chrono::milliseconds timeout, WorldOutboundReadBatch* outBatch) noexcept;
        [[nodiscard]] WorldOutboundDoubleBufferExchangeResult ReleaseRead(std::uint64_t epoch) noexcept;

        [[nodiscard]] WorldOutboundBatchCapacity CapacityPerSlot() const noexcept;
        [[nodiscard]] std::size_t ConfiguredSlotCount() const noexcept;
        [[nodiscard]] WorldOutboundBatchUsage WritableUsage() const noexcept;
        [[nodiscard]] WorldOutboundAppendFailure LastAppendFailure() const noexcept;
        void FinishWrites() noexcept;
        void Close() noexcept;

    private:
        enum class SlotState : std::uint8_t
        {
            Empty = 0,
            Writing,
            Sealed,
            Reading,
        };

        struct Slot final
        {
            explicit Slot(const WorldOutboundBatchCapacity& capacity);

            std::vector<WorldOutboundRecord> records;
            std::vector<psnr::runtime::NrSessionSendChannel> recipients;
            std::vector<std::byte> payloadBytes;
            WorldOutboundBatchUsage usage{};
            std::uint32_t firstServerTick = 0;
            std::uint32_t lastServerTick = 0;
            std::uint32_t replicationRecipientCount = 0;
            std::uint64_t epoch = 0;
            SlotState state = SlotState::Empty;
        };

        explicit WorldOutboundDoubleBuffer(const WorldOutboundBatchCapacity& capacity,
                                           WorldOutboundBufferSlotCount slotCount);

        [[nodiscard]] WorldOutboundAppendResult RecordAppendFailure(
            WorldOutboundWriteOperation operation, WorldOutboundAppendResult result,
            psnr::core::NrPacketType packetType, std::uint32_t serverTick, std::size_t requestedRecipientCount,
            std::size_t requestedPayloadByteCount, WorldOutboundRecordMetadata metadata = {}) noexcept;

        [[nodiscard]] std::size_t FindEmptySlotIndexLocked() const noexcept;
        void EnqueueReadySlotLocked(std::size_t slotIndex) noexcept;
        [[nodiscard]] std::size_t DequeueReadySlotLocked() noexcept;

        static constexpr std::size_t MaximumSlotCount = 3;
        static constexpr std::size_t InvalidSlotIndex = MaximumSlotCount;

        WorldOutboundBatchCapacity capacity_{};
        std::vector<Slot> slots_;
        std::array<std::size_t, MaximumSlotCount> readySlotIndices_{};
        std::size_t readyHeadIndex_ = 0;
        std::size_t readySlotCount_ = 0;
        std::size_t writeSlotIndex_ = InvalidSlotIndex;
        std::size_t readSlotIndex_ = InvalidSlotIndex;
        WorldOutboundAppendFailure lastAppendFailure_{};
        std::uint64_t lastSealedEpoch_ = 0;
        std::atomic<bool> writesFinished_ = false;
        std::atomic<bool> closed_ = false;
        mutable std::mutex mutex_;
        std::condition_variable condition_;
        bool writeSlotActive_ = false;
        bool readClaimActive_ = false;
    };
} // namespace psnr::world
