#include "pch.h"

#include <PrivateServer/NetworkRuntime/NrGateway.h>
#include "NrServerTestUtils.h"

#include <array>
#include <span>

namespace psnr::runtime
{
    using psnr::core::NrErrorCode;
    using psnr::core::NrStatus;

    namespace
    {
        constexpr NrPacketType TestPacketType{0x1234};

        [[nodiscard]] NrGateway CreateRunningGateway(NrServer& server)
        {
            EXPECT_TRUE(server.Start().Succeeded());
            NrGateway gateway;
            EXPECT_TRUE(server.CreateGateway(&gateway).Succeeded());
            return gateway;
        }

        [[nodiscard]] std::span<const std::byte> TestBytes()
        {
            static constexpr std::array<std::byte, 3> Bytes = {
                std::byte{0x11},
                std::byte{0x22},
                std::byte{0x33},
            };
            return std::span<const std::byte>(Bytes);
        }

        [[nodiscard]] NrByteView TestPayloadView()
        {
            const std::span<const std::byte> bytes = TestBytes();
            return NrByteView{bytes.data(), static_cast<std::uint32_t>(bytes.size())};
        }

        [[nodiscard]] NrSessionSendChannelView MakeChannelView(
            const std::span<const NrSessionSendChannel> channels) noexcept
        {
            return NrSessionSendChannelView{channels.data(), static_cast<std::uint32_t>(channels.size())};
        }

        TEST(NrGatewayTests, DefaultGatewayIsInvalid)
        {
            NrGateway gateway;

            EXPECT_FALSE(gateway.IsValid());
            const NrStatus status = gateway.Submit(NrSessionSendChannel{}, TestPacketType, TestPayloadView());

            EXPECT_TRUE(status.Failed());
            EXPECT_EQ(status.ErrorCode(), NrErrorCode::InvalidState);
        }

        TEST(NrGatewayTests, CreatedServerRejectsGatewayCreation)
        {
            NrServer server = tests::CreateServer();
            NrGateway gateway;

            const NrStatus status = server.CreateGateway(&gateway);

            EXPECT_TRUE(status.Failed());
            EXPECT_EQ(status.ErrorCode(), NrErrorCode::InvalidState);
            EXPECT_FALSE(gateway.IsValid());
        }

        TEST(NrGatewayTests, CreateGatewayRejectsNullOutput)
        {
            NrServer server = tests::CreateServer();

            const NrStatus status = server.CreateGateway(nullptr);

            EXPECT_TRUE(status.Failed());
            EXPECT_EQ(status.ErrorCode(), NrErrorCode::InvalidArgument);
        }

        TEST(NrGatewayTests, RunningServerCreatesGateway)
        {
            NrServer server = tests::CreateServer();
            NrGateway gateway = CreateRunningGateway(server);

            EXPECT_TRUE(gateway.IsValid());
        }

        TEST(NrGatewayTests, CreateGatewayRejectsAlreadyValidOutput)
        {
            NrServer server = tests::CreateServer();
            NrGateway gateway = CreateRunningGateway(server);
            ASSERT_TRUE(gateway.IsValid());

            const NrStatus status = server.CreateGateway(&gateway);

            EXPECT_TRUE(status.Failed());
            EXPECT_EQ(status.ErrorCode(), NrErrorCode::InvalidState);
        }

        TEST(NrGatewayTests, GatewayOutlivesServerAndRejectsSubmit)
        {
            NrGateway gateway;
            {
                NrServer server = tests::CreateServer();
                EXPECT_TRUE(server.Start().Succeeded());
                EXPECT_TRUE(server.CreateGateway(&gateway).Succeeded());
            }

            ASSERT_TRUE(gateway.IsValid());
            const NrStatus status = gateway.Submit(NrSessionSendChannel{}, TestPacketType, TestPayloadView());

            EXPECT_TRUE(status.Failed());
            EXPECT_EQ(status.ErrorCode(), NrErrorCode::InvalidState);
        }

        TEST(NrGatewayTests, ServerStopInvalidatesExistingGatewayAndRejectsNewGateway)
        {
            NrServer server = tests::CreateServer();
            NrGateway gateway = CreateRunningGateway(server);
            ASSERT_TRUE(gateway.IsValid());
            ASSERT_TRUE(server.RequestStop().Succeeded());

            NrGateway rejectedGateway;
            const NrStatus createStatus = server.CreateGateway(&rejectedGateway);
            const NrStatus submitStatus = gateway.Submit(NrSessionSendChannel{}, TestPacketType, TestPayloadView());

            EXPECT_EQ(createStatus.ErrorCode(), NrErrorCode::InvalidState);
            EXPECT_FALSE(rejectedGateway.IsValid());
            EXPECT_EQ(submitStatus.ErrorCode(), NrErrorCode::InvalidState);
        }

        TEST(NrGatewayTests, SubmitRejectsInvalidChannel)
        {
            NrServer server = tests::CreateServer();
            NrGateway gateway = CreateRunningGateway(server);
            ASSERT_TRUE(gateway.IsValid());

            const NrStatus status = gateway.Submit(NrSessionSendChannel{}, TestPacketType, TestPayloadView());

            EXPECT_TRUE(status.Failed());
            EXPECT_EQ(status.ErrorCode(), NrErrorCode::InvalidState);
        }

        TEST(NrGatewayTests, SubmitAllowsEmptySemanticPayloadBeforeChannelValidation)
        {
            NrServer server = tests::CreateServer();
            NrGateway gateway = CreateRunningGateway(server);
            ASSERT_TRUE(gateway.IsValid());

            const NrStatus status = gateway.Submit(NrSessionSendChannel{}, TestPacketType, {});

            EXPECT_TRUE(status.Failed());
            EXPECT_EQ(status.ErrorCode(), NrErrorCode::InvalidState);
        }

        TEST(NrGatewayTests, SubmitRejectsMalformedPayloadView)
        {
            NrServer server = tests::CreateServer();
            NrGateway gateway = CreateRunningGateway(server);

            const NrStatus status = gateway.Submit(NrSessionSendChannel{}, TestPacketType, NrByteView{nullptr, 1});

            EXPECT_EQ(status.ErrorCode(), NrErrorCode::InvalidArgument);
        }

        TEST(NrGatewayTests, SubmitRejectsSemanticPayloadThatExceedsWireLimit)
        {
            NrServer server = tests::CreateServer();
            NrGateway gateway = CreateRunningGateway(server);
            static constexpr std::byte Sentinel{};

            const NrStatus status =
                gateway.Submit(NrSessionSendChannel{}, TestPacketType, NrByteView{&Sentinel, 8187});

            EXPECT_EQ(status.ErrorCode(), NrErrorCode::InvalidArgument);
        }

        TEST(NrGatewayTests, SubmitManyWithEmptyChannelsReturnsNoopReport)
        {
            NrServer server = tests::CreateServer();
            NrGateway gateway = CreateRunningGateway(server);
            ASSERT_TRUE(gateway.IsValid());

            NrGatewaySendReport report{9, 8, 7};
            const NrStatus status = gateway.SubmitMany({}, TestPacketType, {}, report);

            ASSERT_TRUE(status.Succeeded());
            EXPECT_EQ(report.attempted, 0u);
            EXPECT_EQ(report.accepted, 0u);
            EXPECT_EQ(report.rejected, 0u);
        }

        TEST(NrGatewayTests, SubmitManyAllowsEmptySemanticPayload)
        {
            const std::array<NrSessionSendChannel, 1> channels = {NrSessionSendChannel{}};
            NrServer server = tests::CreateServer();
            NrGateway gateway = CreateRunningGateway(server);
            ASSERT_TRUE(gateway.IsValid());

            NrGatewaySendReport report;
            const NrStatus status = gateway.SubmitMany(MakeChannelView(std::span(channels)), TestPacketType, {}, report);

            EXPECT_TRUE(status.Succeeded());
            EXPECT_EQ(report.attempted, 1u);
            EXPECT_EQ(report.accepted, 0u);
            EXPECT_EQ(report.rejected, 1u);
        }

        TEST(NrGatewayTests, SubmitManyCountsInvalidChannelsAsRejectedRecipients)
        {
            const std::array<NrSessionSendChannel, 2> channels = {
                NrSessionSendChannel{},
                NrSessionSendChannel{},
            };
            NrServer server = tests::CreateServer();
            NrGateway gateway = CreateRunningGateway(server);
            ASSERT_TRUE(gateway.IsValid());

            NrGatewaySendReport report;
            const NrStatus status =
                gateway.SubmitMany(MakeChannelView(std::span(channels)), TestPacketType, TestPayloadView(), report);

            ASSERT_TRUE(status.Succeeded());
            EXPECT_EQ(report.attempted, 2u);
            EXPECT_EQ(report.accepted, 0u);
            EXPECT_EQ(report.rejected, 2u);
        }

        TEST(NrGatewayTests, SubmitManyFailureDoesNotChangeOutputReport)
        {
            NrServer server = tests::CreateServer();
            NrGateway gateway = CreateRunningGateway(server);
            NrGatewaySendReport report{9, 8, 7};

            const NrStatus status =
                gateway.SubmitMany(NrSessionSendChannelView{nullptr, 1}, TestPacketType, TestPayloadView(), report);

            EXPECT_EQ(status.ErrorCode(), NrErrorCode::InvalidArgument);
            EXPECT_EQ(report.attempted, 9u);
            EXPECT_EQ(report.accepted, 8u);
            EXPECT_EQ(report.rejected, 7u);
        }
    } // namespace
} // namespace psnr::runtime
