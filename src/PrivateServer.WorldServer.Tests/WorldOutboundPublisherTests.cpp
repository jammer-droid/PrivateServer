#include "pch.h"

#include "WorldOutboundPublisher.h"

#include <PrivateServer/NetworkRuntime/NrErrorCode.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace psnr::world::tests
{
    namespace
    {
        struct FakePublishCall final
        {
            std::uint16_t packetType = 0;
            std::uint32_t recipientCount = 0;
            std::vector<std::byte> payload;
        };

        class FakeOutboundGateway final
        {
        public:
            explicit FakeOutboundGateway(std::vector<psnr::runtime::NrGatewaySendReport> reports,
                                         const std::size_t failureCallIndex = NoFailure)
                : reports_(std::move(reports))
                , failureCallIndex_(failureCallIndex)
            {
            }

            [[nodiscard]] psnr::core::NrStatus SubmitMany(const psnr::runtime::NrSessionSendChannelView channels,
                                                          const psnr::core::NrPacketType packetType,
                                                          const psnr::runtime::NrByteView payload,
                                                          psnr::runtime::NrGatewaySendReport& outReport) noexcept
            {
                calls_.push_back(FakePublishCall{
                    packetType.value,
                    channels.size,
                    std::vector<std::byte>{payload.data, payload.data + payload.size},
                });
                const std::size_t callIndex = calls_.size() - 1;
                if (callIndex == failureCallIndex_)
                {
                    return psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::InvalidState);
                }

                outReport = reports_[callIndex];
                return psnr::core::NrStatus::Success();
            }

            [[nodiscard]] const std::vector<FakePublishCall>& Calls() const noexcept
            {
                return calls_;
            }

            static constexpr std::size_t NoFailure = static_cast<std::size_t>(-1);

        private:
            std::vector<psnr::runtime::NrGatewaySendReport> reports_;
            std::size_t failureCallIndex_ = NoFailure;
            std::vector<FakePublishCall> calls_;
        };

        [[nodiscard]] std::unique_ptr<WorldOutboundDoubleBuffer> CreatePublisherBuffer()
        {
            WorldResult<std::unique_ptr<WorldOutboundDoubleBuffer>> result =
                WorldOutboundDoubleBuffer::Create(WorldOutboundBatchCapacity{3, 4, 16});
            EXPECT_TRUE(result.Succeeded());
            return result.Failed() ? nullptr : result.TakeValue();
        }
    } // namespace

    TEST(WorldOutboundPublisherTests, PublishesRecordsInOrderAndAggregatesRecipientReport)
    {
        std::unique_ptr<WorldOutboundDoubleBuffer> buffer = CreatePublisherBuffer();
        ASSERT_NE(buffer, nullptr);
        const std::array<psnr::runtime::NrSessionSendChannel, 2> firstRecipients{};
        const std::array<psnr::runtime::NrSessionSendChannel, 1> secondRecipients{};
        const std::array<std::byte, 2> firstPayload{std::byte{1}, std::byte{2}};
        const std::array<std::byte, 1> secondPayload{std::byte{3}};
        ASSERT_EQ(buffer->TryAppend(psnr::core::NrPacketType{0x0182}, firstRecipients, firstPayload),
                  WorldOutboundAppendResult::Appended);
        ASSERT_EQ(buffer->TryAppend(psnr::core::NrPacketType{0x0183}, secondRecipients, secondPayload),
                  WorldOutboundAppendResult::Appended);
        ASSERT_EQ(buffer->SealWrite(20), WorldOutboundDoubleBufferExchangeResult::Exchanged);

        FakeOutboundGateway gateway{
            {
                psnr::runtime::NrGatewaySendReport{2, 1, 1},
                psnr::runtime::NrGatewaySendReport{1, 1, 0},
            },
        };
        WorldOutboundPublisher publisher{*buffer};

        const WorldOutboundPublishReport report = publisher.PublishNext(gateway, std::chrono::milliseconds{0});

        EXPECT_EQ(report.stopReason, WorldOutboundPublishStopReason::Published);
        EXPECT_EQ(report.epoch, 20u);
        EXPECT_EQ(report.recordCount, 2u);
        EXPECT_EQ(report.processedRecordCount, 2u);
        EXPECT_EQ(report.discardedRecordCount, 0u);
        EXPECT_EQ(report.attemptedRecipientCount, 3u);
        EXPECT_EQ(report.acceptedRecipientCount, 2u);
        EXPECT_EQ(report.rejectedRecipientCount, 1u);
        EXPECT_EQ(report.discardedRecipientCount, 0u);
        ASSERT_EQ(gateway.Calls().size(), 2u);
        EXPECT_EQ(gateway.Calls()[0].packetType, 0x0182);
        EXPECT_EQ(gateway.Calls()[0].recipientCount, 2u);
        EXPECT_EQ(gateway.Calls()[0].payload, (std::vector<std::byte>{std::byte{1}, std::byte{2}}));
        EXPECT_EQ(gateway.Calls()[1].packetType, 0x0183);
        EXPECT_EQ(gateway.Calls()[1].recipientCount, 1u);

        const WorldOutboundPublisherMetrics metrics = publisher.Metrics();
        EXPECT_EQ(metrics.publishedBatchCount, 1u);
        EXPECT_EQ(metrics.publishedRecordCount, 2u);
        EXPECT_EQ(metrics.discardedRecordCount, 0u);
        EXPECT_EQ(metrics.acceptedRecipientCount, 2u);
        EXPECT_EQ(metrics.rejectedRecipientCount, 1u);
        EXPECT_EQ(metrics.discardedRecipientCount, 0u);
        EXPECT_EQ(metrics.recordHighWatermark, 2u);
        EXPECT_EQ(metrics.recipientHighWatermark, 3u);
        EXPECT_EQ(metrics.payloadByteHighWatermark, 3u);

        EXPECT_EQ(publisher.PublishNext(gateway, std::chrono::milliseconds{0}).stopReason,
                  WorldOutboundPublishStopReason::NoBatch);
    }

    TEST(WorldOutboundPublisherTests, PublicationFailureReleasesBatchAndReportsDiscardedRemainder)
    {
        std::unique_ptr<WorldOutboundDoubleBuffer> buffer = CreatePublisherBuffer();
        ASSERT_NE(buffer, nullptr);
        const std::array<psnr::runtime::NrSessionSendChannel, 1> recipients{};
        const std::array<std::byte, 1> firstPayload{std::byte{1}};
        const std::array<std::byte, 1> secondPayload{std::byte{2}};
        ASSERT_EQ(buffer->TryAppend(psnr::core::NrPacketType{1}, recipients, firstPayload),
                  WorldOutboundAppendResult::Appended);
        ASSERT_EQ(buffer->TryAppend(psnr::core::NrPacketType{2}, recipients, secondPayload),
                  WorldOutboundAppendResult::Appended);
        ASSERT_EQ(buffer->SealWrite(30), WorldOutboundDoubleBufferExchangeResult::Exchanged);

        FakeOutboundGateway gateway{
            {
                psnr::runtime::NrGatewaySendReport{1, 1, 0},
                psnr::runtime::NrGatewaySendReport{},
            },
            1,
        };
        WorldOutboundPublisher publisher{*buffer};

        const WorldOutboundPublishReport report = publisher.PublishNext(gateway, std::chrono::milliseconds{0});

        EXPECT_EQ(report.stopReason, WorldOutboundPublishStopReason::PublicationFailed);
        EXPECT_EQ(report.recordCount, 2u);
        EXPECT_EQ(report.processedRecordCount, 1u);
        EXPECT_EQ(report.discardedRecordCount, 1u);
        EXPECT_EQ(report.attemptedRecipientCount, 1u);
        EXPECT_EQ(report.acceptedRecipientCount, 1u);
        EXPECT_EQ(report.rejectedRecipientCount, 0u);
        EXPECT_EQ(report.discardedRecipientCount, 1u);
        EXPECT_EQ(report.publicationStatus.ErrorCode(), psnr::core::NrErrorCode::InvalidState);
        const WorldOutboundPublisherMetrics metrics = publisher.Metrics();
        EXPECT_EQ(metrics.publishedBatchCount, 0u);
        EXPECT_EQ(metrics.publicationFailureCount, 1u);
        EXPECT_EQ(metrics.publishedRecordCount, 1u);
        EXPECT_EQ(metrics.discardedRecordCount, 1u);
        EXPECT_EQ(metrics.acceptedRecipientCount, 1u);
        EXPECT_EQ(metrics.discardedRecipientCount, 1u);

        ASSERT_EQ(buffer->WaitPrepareWrite(std::chrono::milliseconds::zero()),
                  WorldOutboundDoubleBufferExchangeResult::Exchanged);
        ASSERT_EQ(buffer->TryAppend(psnr::core::NrPacketType{3}, recipients, firstPayload),
                  WorldOutboundAppendResult::Appended);
        EXPECT_EQ(buffer->SealWrite(31), WorldOutboundDoubleBufferExchangeResult::Exchanged);
    }

    TEST(WorldOutboundPublisherTests, AcceptsPublicRuntimeGatewayBoundary)
    {
        std::unique_ptr<WorldOutboundDoubleBuffer> buffer = CreatePublisherBuffer();
        ASSERT_NE(buffer, nullptr);
        psnr::runtime::NrGateway gateway;
        WorldOutboundPublisher publisher{*buffer};

        EXPECT_EQ(publisher.PublishNext(gateway, std::chrono::milliseconds{0}).stopReason,
                  WorldOutboundPublishStopReason::NoBatch);
    }
} // namespace psnr::world::tests
