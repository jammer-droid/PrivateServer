#include "pch.h"

#include "WorldOutboundDoubleBuffer.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>

namespace psnr::world::tests
{
    namespace
    {
        [[nodiscard]] std::unique_ptr<WorldOutboundDoubleBuffer> CreateOutboundBuffer(
            const WorldOutboundBatchCapacity& capacity,
            const WorldOutboundBufferSlotCount slotCount = WorldOutboundBufferSlotCount::Double)
        {
            WorldResult<std::unique_ptr<WorldOutboundDoubleBuffer>> result =
                WorldOutboundDoubleBuffer::Create(capacity, slotCount);
            EXPECT_TRUE(result.Succeeded());
            return result.Failed() ? nullptr : result.TakeValue();
        }

        [[nodiscard]] bool EncodeSentinel(void* const context, const std::span<std::byte> output) noexcept
        {
            if (context == nullptr || output.size() != 2)
            {
                return false;
            }
            output[0] = std::byte{7};
            output[1] = std::byte{9};
            return true;
        }

        [[nodiscard]] bool RejectEncoding(void*, std::span<std::byte>) noexcept
        {
            return false;
        }
    } // namespace

    TEST(WorldOutboundDoubleBufferTests, RejectsInvalidCreationArguments)
    {
        const WorldResult<std::unique_ptr<WorldOutboundDoubleBuffer>> result =
            WorldOutboundDoubleBuffer::Create(WorldOutboundBatchCapacity{});

        ASSERT_TRUE(result.Failed());
        EXPECT_EQ(result.Error(), WorldErrorCode::InvalidArgument);

        const WorldResult<std::unique_ptr<WorldOutboundDoubleBuffer>> invalidSlotCountResult =
            WorldOutboundDoubleBuffer::Create(WorldOutboundBatchCapacity{1, 1, 1},
                                              static_cast<WorldOutboundBufferSlotCount>(4));
        ASSERT_TRUE(invalidSlotCountResult.Failed());
        EXPECT_EQ(invalidSlotCountResult.Error(), WorldErrorCode::InvalidArgument);
    }

    TEST(WorldOutboundDoubleBufferTests, AppendsOwnedRecordRangesAndAlternatesSlots)
    {
        const WorldOutboundBatchCapacity capacity{2, 3, 8};
        std::unique_ptr<WorldOutboundDoubleBuffer> buffer = CreateOutboundBuffer(capacity);
        ASSERT_NE(buffer, nullptr);
        const std::array<psnr::runtime::NrSessionSendChannel, 2> firstRecipients{};
        const std::array<psnr::runtime::NrSessionSendChannel, 1> secondRecipients{};
        const std::array<std::byte, 3> firstPayload{std::byte{1}, std::byte{2}, std::byte{3}};
        const std::array<std::byte, 2> secondPayload{std::byte{4}, std::byte{5}};

        ASSERT_EQ(buffer->BeginWriteBatch(10, 100, 103), WorldOutboundDoubleBufferExchangeResult::Exchanged);
        ASSERT_EQ(buffer->TryAppend(psnr::core::NrPacketType{0x0182}, firstRecipients, firstPayload),
                  WorldOutboundAppendResult::Appended);
        ASSERT_EQ(buffer->TryAppend(psnr::core::NrPacketType{0x0183}, secondRecipients, secondPayload),
                  WorldOutboundAppendResult::Appended);
        EXPECT_EQ(buffer->WritableUsage(), (WorldOutboundBatchUsage{2, 3, 5}));
        ASSERT_EQ(buffer->SealWrite(10), WorldOutboundDoubleBufferExchangeResult::Exchanged);

        WorldOutboundReadBatch readBatch;
        ASSERT_EQ(buffer->WaitAcquireRead(std::chrono::milliseconds{0}, &readBatch),
                  WorldOutboundDoubleBufferExchangeResult::Exchanged);
        ASSERT_EQ(readBatch.epoch, 10u);
        EXPECT_EQ(readBatch.firstServerTick, 100u);
        EXPECT_EQ(readBatch.lastServerTick, 103u);
        ASSERT_EQ(readBatch.records.size(), 2u);
        EXPECT_EQ(readBatch.records[0], (WorldOutboundRecord{psnr::core::NrPacketType{0x0182}, 0, 2, 0, 3}));
        EXPECT_EQ(readBatch.records[1], (WorldOutboundRecord{psnr::core::NrPacketType{0x0183}, 2, 1, 3, 2}));
        EXPECT_TRUE(std::equal(firstPayload.begin(), firstPayload.end(), readBatch.payloadBytes.begin()));
        EXPECT_TRUE(std::equal(secondPayload.begin(), secondPayload.end(), readBatch.payloadBytes.begin() + 3));

        ASSERT_EQ(buffer->WaitPrepareWrite(std::chrono::milliseconds::zero()),
                  WorldOutboundDoubleBufferExchangeResult::Exchanged);
        ASSERT_EQ(buffer->BeginWriteBatch(11, 104, 104), WorldOutboundDoubleBufferExchangeResult::Exchanged);
        ASSERT_EQ(buffer->TryAppend(psnr::core::NrPacketType{0x0184}, secondRecipients, secondPayload),
                  WorldOutboundAppendResult::Appended);
        EXPECT_EQ(buffer->SealWrite(11), WorldOutboundDoubleBufferExchangeResult::Exchanged);
        EXPECT_EQ(buffer->WaitPrepareWrite(std::chrono::milliseconds::zero()),
                  WorldOutboundDoubleBufferExchangeResult::Busy);
        ASSERT_EQ(buffer->ReleaseRead(10), WorldOutboundDoubleBufferExchangeResult::Exchanged);
        EXPECT_EQ(buffer->WaitPrepareWrite(std::chrono::milliseconds::zero()),
                  WorldOutboundDoubleBufferExchangeResult::Exchanged);
    }

    TEST(WorldOutboundDoubleBufferTests, CapacityFailureDoesNotPartiallyAppend)
    {
        std::unique_ptr<WorldOutboundDoubleBuffer> buffer = CreateOutboundBuffer(WorldOutboundBatchCapacity{2, 2, 4});
        ASSERT_NE(buffer, nullptr);
        const std::array<psnr::runtime::NrSessionSendChannel, 1> recipients{};
        const std::array<std::byte, 2> payload{std::byte{1}, std::byte{2}};
        const std::array<std::byte, 3> oversizedPayload{std::byte{3}, std::byte{4}, std::byte{5}};

        ASSERT_EQ(buffer->BeginWriteBatch(1, 1, 1), WorldOutboundDoubleBufferExchangeResult::Exchanged);
        ASSERT_EQ(buffer->TryAppend(psnr::core::NrPacketType{1}, recipients, payload),
                  WorldOutboundAppendResult::Appended);
        const WorldOutboundBatchUsage beforeFailure = buffer->WritableUsage();
        EXPECT_EQ(buffer->TryAppend(psnr::core::NrPacketType{2}, recipients, oversizedPayload),
                  WorldOutboundAppendResult::CapacityExceeded);
        EXPECT_EQ(buffer->WritableUsage(), beforeFailure);
        const WorldOutboundAppendFailure failure = buffer->LastAppendFailure();
        EXPECT_TRUE(failure.Present());
        EXPECT_EQ(failure.operation, WorldOutboundWriteOperation::Append);
        EXPECT_EQ(failure.result, WorldOutboundAppendResult::CapacityExceeded);
        EXPECT_EQ(failure.packetType.value, 2u);
        EXPECT_EQ(failure.requestedRecipientCount, 1u);
        EXPECT_EQ(failure.requestedPayloadByteCount, 3u);
        EXPECT_EQ(failure.usage, beforeFailure);

        ASSERT_EQ(buffer->TryAppend(psnr::core::NrPacketType{2}, recipients, payload),
                  WorldOutboundAppendResult::Appended);
        EXPECT_EQ(buffer->TryAppend(psnr::core::NrPacketType{3}, recipients, {}),
                  WorldOutboundAppendResult::CapacityExceeded);
        EXPECT_EQ(buffer->WritableUsage(), (WorldOutboundBatchUsage{2, 2, 4}));
    }

    TEST(WorldOutboundDoubleBufferTests, SealsNextBatchAndWaitsForWritableSlotAfterPreviousRead)
    {
        constexpr std::chrono::milliseconds WaitTimeout{500};
        std::unique_ptr<WorldOutboundDoubleBuffer> buffer = CreateOutboundBuffer(WorldOutboundBatchCapacity{1, 1, 1});
        ASSERT_NE(buffer, nullptr);
        const std::array<psnr::runtime::NrSessionSendChannel, 1> recipients{};
        const std::array<std::byte, 1> firstPayload{std::byte{1}};
        const std::array<std::byte, 1> secondPayload{std::byte{2}};

        ASSERT_EQ(buffer->BeginWriteBatch(1, 1, 1), WorldOutboundDoubleBufferExchangeResult::Exchanged);
        ASSERT_EQ(buffer->TryAppend(psnr::core::NrPacketType{1}, recipients, firstPayload),
                  WorldOutboundAppendResult::Appended);
        ASSERT_EQ(buffer->SealWrite(1), WorldOutboundDoubleBufferExchangeResult::Exchanged);

        WorldOutboundReadBatch firstRead;
        ASSERT_EQ(buffer->WaitAcquireRead(std::chrono::milliseconds::zero(), &firstRead),
                  WorldOutboundDoubleBufferExchangeResult::Exchanged);

        ASSERT_EQ(buffer->WaitPrepareWrite(std::chrono::milliseconds::zero()),
                  WorldOutboundDoubleBufferExchangeResult::Exchanged);
        ASSERT_EQ(buffer->BeginWriteBatch(2, 2, 2), WorldOutboundDoubleBufferExchangeResult::Exchanged);
        ASSERT_EQ(buffer->TryAppend(psnr::core::NrPacketType{2}, recipients, secondPayload),
                  WorldOutboundAppendResult::Appended);

        ASSERT_EQ(buffer->SealWrite(2), WorldOutboundDoubleBufferExchangeResult::Exchanged);
        std::future<WorldOutboundDoubleBufferExchangeResult> nextWrite =
            std::async(std::launch::async, [&buffer, WaitTimeout]() { return buffer->WaitPrepareWrite(WaitTimeout); });
        EXPECT_EQ(nextWrite.wait_for(std::chrono::milliseconds{20}), std::future_status::timeout);

        ASSERT_EQ(buffer->ReleaseRead(1), WorldOutboundDoubleBufferExchangeResult::Exchanged);
        ASSERT_EQ(nextWrite.get(), WorldOutboundDoubleBufferExchangeResult::Exchanged);

        WorldOutboundReadBatch secondRead;
        ASSERT_EQ(buffer->WaitAcquireRead(std::chrono::milliseconds::zero(), &secondRead),
                  WorldOutboundDoubleBufferExchangeResult::Exchanged);
        ASSERT_EQ(secondRead.payloadBytes.size(), 1u);
        EXPECT_EQ(secondRead.payloadBytes[0], std::byte{2});
        EXPECT_EQ(buffer->ReleaseRead(2), WorldOutboundDoubleBufferExchangeResult::Exchanged);
    }

    TEST(WorldOutboundDoubleBufferTests, TripleBufferSealsTwoBatchesWhilePreviousReadRemainsActive)
    {
        std::unique_ptr<WorldOutboundDoubleBuffer> buffer =
            CreateOutboundBuffer(WorldOutboundBatchCapacity{1, 1, 1}, WorldOutboundBufferSlotCount::Triple);
        ASSERT_NE(buffer, nullptr);
        EXPECT_EQ(buffer->ConfiguredSlotCount(), 3u);
        const std::array<psnr::runtime::NrSessionSendChannel, 1> recipients{};
        const std::array<std::byte, 1> firstPayload{std::byte{1}};
        const std::array<std::byte, 1> secondPayload{std::byte{2}};
        const std::array<std::byte, 1> thirdPayload{std::byte{3}};

        ASSERT_EQ(buffer->BeginWriteBatch(1, 1, 1), WorldOutboundDoubleBufferExchangeResult::Exchanged);
        ASSERT_EQ(buffer->TryAppend(psnr::core::NrPacketType{1}, recipients, firstPayload),
                  WorldOutboundAppendResult::Appended);
        ASSERT_EQ(buffer->SealWrite(1), WorldOutboundDoubleBufferExchangeResult::Exchanged);

        WorldOutboundReadBatch firstRead;
        ASSERT_EQ(buffer->WaitAcquireRead(std::chrono::milliseconds::zero(), &firstRead),
                  WorldOutboundDoubleBufferExchangeResult::Exchanged);

        ASSERT_EQ(buffer->WaitPrepareWrite(std::chrono::milliseconds::zero()),
                  WorldOutboundDoubleBufferExchangeResult::Exchanged);
        ASSERT_EQ(buffer->BeginWriteBatch(2, 2, 2), WorldOutboundDoubleBufferExchangeResult::Exchanged);
        ASSERT_EQ(buffer->TryAppend(psnr::core::NrPacketType{2}, recipients, secondPayload),
                  WorldOutboundAppendResult::Appended);
        EXPECT_EQ(buffer->SealWrite(2), WorldOutboundDoubleBufferExchangeResult::Exchanged);

        ASSERT_EQ(buffer->WaitPrepareWrite(std::chrono::milliseconds::zero()),
                  WorldOutboundDoubleBufferExchangeResult::Exchanged);
        ASSERT_EQ(buffer->BeginWriteBatch(3, 3, 3), WorldOutboundDoubleBufferExchangeResult::Exchanged);
        ASSERT_EQ(buffer->TryAppend(psnr::core::NrPacketType{3}, recipients, thirdPayload),
                  WorldOutboundAppendResult::Appended);
        EXPECT_EQ(buffer->SealWrite(3), WorldOutboundDoubleBufferExchangeResult::Exchanged);
        EXPECT_EQ(buffer->WaitPrepareWrite(std::chrono::milliseconds::zero()),
                  WorldOutboundDoubleBufferExchangeResult::Busy);

        ASSERT_EQ(buffer->ReleaseRead(1), WorldOutboundDoubleBufferExchangeResult::Exchanged);
        ASSERT_EQ(buffer->WaitPrepareWrite(std::chrono::milliseconds::zero()),
                  WorldOutboundDoubleBufferExchangeResult::Exchanged);

        WorldOutboundReadBatch secondRead;
        ASSERT_EQ(buffer->WaitAcquireRead(std::chrono::milliseconds::zero(), &secondRead),
                  WorldOutboundDoubleBufferExchangeResult::Exchanged);
        EXPECT_EQ(secondRead.epoch, 2u);
        ASSERT_EQ(secondRead.payloadBytes.size(), 1u);
        EXPECT_EQ(secondRead.payloadBytes[0], std::byte{2});
        ASSERT_EQ(buffer->ReleaseRead(2), WorldOutboundDoubleBufferExchangeResult::Exchanged);

        WorldOutboundReadBatch thirdRead;
        ASSERT_EQ(buffer->WaitAcquireRead(std::chrono::milliseconds::zero(), &thirdRead),
                  WorldOutboundDoubleBufferExchangeResult::Exchanged);
        EXPECT_EQ(thirdRead.epoch, 3u);
        ASSERT_EQ(thirdRead.payloadBytes.size(), 1u);
        EXPECT_EQ(thirdRead.payloadBytes[0], std::byte{3});
        EXPECT_EQ(buffer->ReleaseRead(3), WorldOutboundDoubleBufferExchangeResult::Exchanged);
    }

    TEST(WorldOutboundDoubleBufferTests, EmptyTickDoesNotSealOrSwap)
    {
        std::unique_ptr<WorldOutboundDoubleBuffer> buffer = CreateOutboundBuffer(WorldOutboundBatchCapacity{1, 1, 1});
        ASSERT_NE(buffer, nullptr);

        ASSERT_EQ(buffer->BeginWriteBatch(1, 1, 1), WorldOutboundDoubleBufferExchangeResult::Exchanged);
        EXPECT_EQ(buffer->SealWrite(1), WorldOutboundDoubleBufferExchangeResult::Empty);
        EXPECT_EQ(buffer->BeginWriteBatch(2, 2, 2), WorldOutboundDoubleBufferExchangeResult::Exchanged);
        WorldOutboundReadBatch readBatch;
        EXPECT_EQ(buffer->WaitAcquireRead(std::chrono::milliseconds{0}, &readBatch),
                  WorldOutboundDoubleBufferExchangeResult::TimedOut);
        EXPECT_EQ(buffer->WritableUsage(), WorldOutboundBatchUsage{});
    }

    TEST(WorldOutboundDoubleBufferTests, RejectsNonIncreasingSealedEpoch)
    {
        std::unique_ptr<WorldOutboundDoubleBuffer> buffer = CreateOutboundBuffer(WorldOutboundBatchCapacity{1, 1, 1});
        ASSERT_NE(buffer, nullptr);
        const std::array<psnr::runtime::NrSessionSendChannel, 1> recipients{};
        const std::array<std::byte, 1> payload{std::byte{1}};

        ASSERT_EQ(buffer->BeginWriteBatch(2, 2, 2), WorldOutboundDoubleBufferExchangeResult::Exchanged);
        ASSERT_EQ(buffer->TryAppend(psnr::core::NrPacketType{1}, recipients, payload),
                  WorldOutboundAppendResult::Appended);
        ASSERT_EQ(buffer->SealWrite(2), WorldOutboundDoubleBufferExchangeResult::Exchanged);

        WorldOutboundReadBatch readBatch;
        ASSERT_EQ(buffer->WaitAcquireRead(std::chrono::milliseconds::zero(), &readBatch),
                  WorldOutboundDoubleBufferExchangeResult::Exchanged);
        ASSERT_EQ(buffer->ReleaseRead(2), WorldOutboundDoubleBufferExchangeResult::Exchanged);

        ASSERT_EQ(buffer->WaitPrepareWrite(std::chrono::milliseconds::zero()),
                  WorldOutboundDoubleBufferExchangeResult::Exchanged);
        EXPECT_EQ(buffer->BeginWriteBatch(2, 3, 3), WorldOutboundDoubleBufferExchangeResult::InvalidState);
        EXPECT_EQ(buffer->BeginWriteBatch(1, 3, 3), WorldOutboundDoubleBufferExchangeResult::InvalidState);
        EXPECT_EQ(buffer->BeginWriteBatch(3, 3, 3), WorldOutboundDoubleBufferExchangeResult::Exchanged);
    }

    TEST(WorldOutboundDoubleBufferTests, CloseWakesWritableSlotWait)
    {
        constexpr std::chrono::milliseconds WaitTimeout{500};
        std::unique_ptr<WorldOutboundDoubleBuffer> buffer = CreateOutboundBuffer(WorldOutboundBatchCapacity{1, 1, 1});
        ASSERT_NE(buffer, nullptr);
        const std::array<psnr::runtime::NrSessionSendChannel, 1> recipients{};
        const std::array<std::byte, 1> payload{std::byte{1}};

        ASSERT_EQ(buffer->BeginWriteBatch(1, 1, 1), WorldOutboundDoubleBufferExchangeResult::Exchanged);
        ASSERT_EQ(buffer->TryAppend(psnr::core::NrPacketType{1}, recipients, payload),
                  WorldOutboundAppendResult::Appended);
        ASSERT_EQ(buffer->SealWrite(1), WorldOutboundDoubleBufferExchangeResult::Exchanged);
        WorldOutboundReadBatch readBatch;
        ASSERT_EQ(buffer->WaitAcquireRead(std::chrono::milliseconds::zero(), &readBatch),
                  WorldOutboundDoubleBufferExchangeResult::Exchanged);

        ASSERT_EQ(buffer->WaitPrepareWrite(std::chrono::milliseconds::zero()),
                  WorldOutboundDoubleBufferExchangeResult::Exchanged);
        ASSERT_EQ(buffer->BeginWriteBatch(2, 2, 2), WorldOutboundDoubleBufferExchangeResult::Exchanged);
        ASSERT_EQ(buffer->TryAppend(psnr::core::NrPacketType{2}, recipients, payload),
                  WorldOutboundAppendResult::Appended);
        ASSERT_EQ(buffer->SealWrite(2), WorldOutboundDoubleBufferExchangeResult::Exchanged);
        std::future<WorldOutboundDoubleBufferExchangeResult> prepareResult =
            std::async(std::launch::async, [&buffer, WaitTimeout]() { return buffer->WaitPrepareWrite(WaitTimeout); });
        ASSERT_EQ(prepareResult.wait_for(std::chrono::milliseconds{20}), std::future_status::timeout);

        buffer->Close();

        EXPECT_EQ(prepareResult.get(), WorldOutboundDoubleBufferExchangeResult::InvalidState);
        EXPECT_EQ(buffer->ReleaseRead(1), WorldOutboundDoubleBufferExchangeResult::Exchanged);
        EXPECT_EQ(buffer->BeginWriteBatch(3, 3, 3), WorldOutboundDoubleBufferExchangeResult::InvalidState);
    }

    TEST(WorldOutboundDoubleBufferTests, FinishWritesAllowsReadyDrainThenReportsEmpty)
    {
        std::unique_ptr<WorldOutboundDoubleBuffer> buffer = CreateOutboundBuffer(WorldOutboundBatchCapacity{1, 1, 1});
        ASSERT_NE(buffer, nullptr);
        const std::array<psnr::runtime::NrSessionSendChannel, 1> recipients{};
        const std::array<std::byte, 1> payload{std::byte{1}};

        ASSERT_EQ(buffer->BeginWriteBatch(1, 1, 1), WorldOutboundDoubleBufferExchangeResult::Exchanged);
        ASSERT_EQ(buffer->TryAppend(psnr::core::NrPacketType{1}, recipients, payload),
                  WorldOutboundAppendResult::Appended);
        ASSERT_EQ(buffer->SealWrite(1), WorldOutboundDoubleBufferExchangeResult::Exchanged);

        buffer->FinishWrites();

        WorldOutboundReadBatch readBatch;
        ASSERT_EQ(buffer->WaitAcquireRead(std::chrono::milliseconds::zero(), &readBatch),
                  WorldOutboundDoubleBufferExchangeResult::Exchanged);
        EXPECT_EQ(readBatch.epoch, 1u);
        ASSERT_EQ(buffer->ReleaseRead(1), WorldOutboundDoubleBufferExchangeResult::Exchanged);
        EXPECT_EQ(buffer->WaitAcquireRead(std::chrono::milliseconds::zero(), &readBatch),
                  WorldOutboundDoubleBufferExchangeResult::Empty);
        EXPECT_EQ(buffer->WaitPrepareWrite(std::chrono::milliseconds::zero()),
                  WorldOutboundDoubleBufferExchangeResult::InvalidState);
    }

    TEST(WorldOutboundDoubleBufferTests, EncodesDirectlyAndDoesNotCommitFailedEncoding)
    {
        std::unique_ptr<WorldOutboundDoubleBuffer> buffer = CreateOutboundBuffer(WorldOutboundBatchCapacity{1, 1, 2});
        ASSERT_NE(buffer, nullptr);
        const std::array<psnr::runtime::NrSessionSendChannel, 1> recipients{};

        ASSERT_EQ(buffer->BeginWriteBatch(1, 1, 1), WorldOutboundDoubleBufferExchangeResult::Exchanged);
        EXPECT_EQ(buffer->TryAppendEncoded(psnr::core::NrPacketType{1}, recipients, 2, RejectEncoding, nullptr),
                  WorldOutboundAppendResult::EncodingFailed);
        EXPECT_EQ(buffer->WritableUsage(), WorldOutboundBatchUsage{});
        const WorldOutboundAppendFailure failure = buffer->LastAppendFailure();
        EXPECT_EQ(failure.operation, WorldOutboundWriteOperation::AppendEncoded);
        EXPECT_EQ(failure.result, WorldOutboundAppendResult::EncodingFailed);
        EXPECT_EQ(failure.packetType.value, 1u);
        EXPECT_EQ(failure.requestedRecipientCount, 1u);
        EXPECT_EQ(failure.requestedPayloadByteCount, 2u);

        std::uint32_t context = 1;
        EXPECT_EQ(buffer->TryAppendEncoded(psnr::core::NrPacketType{1}, recipients, 2, EncodeSentinel, &context),
                  WorldOutboundAppendResult::Appended);
        EXPECT_EQ(buffer->WritableUsage(), (WorldOutboundBatchUsage{1, 1, 2}));
    }

    TEST(WorldOutboundDoubleBufferTests, RequiresBoundBatchBeforeReplicationReservation)
    {
        std::unique_ptr<WorldOutboundDoubleBuffer> buffer = CreateOutboundBuffer(WorldOutboundBatchCapacity{1, 1, 1});
        ASSERT_NE(buffer, nullptr);
        std::uint32_t firstRecipientIndex = 0;

        EXPECT_EQ(buffer->ReserveReplicationRecipients(2, &firstRecipientIndex),
                  WorldOutboundAppendResult::InvalidState);

        const WorldOutboundAppendFailure failure = buffer->LastAppendFailure();
        EXPECT_EQ(failure.operation, WorldOutboundWriteOperation::ReserveReplicationRecipients);
        EXPECT_EQ(failure.result, WorldOutboundAppendResult::InvalidState);
        EXPECT_EQ(failure.serverTick, 0u);
        EXPECT_EQ(failure.existingReplicationServerTick, 0u);
        EXPECT_EQ(failure.existingReplicationRecipientCount, 0u);
        EXPECT_EQ(failure.requestedRecipientCount, 2u);
    }
} // namespace psnr::world::tests
