#include "pch.h"

#include "NrClientTransportEventSink.h"

#include <array>
#include <cstddef>
#include <span>
#include <type_traits>

namespace psnr::runtime::internal
{
    namespace
    {
        enum class NrRecordedClientEventKind
        {
            None,
            TransportConnected,
            TransportConnectionFailed,
            PacketReceived,
            TransportDisconnected,
            PendingPromotion,
        };

        class NrRecordingClientTransportEventSink final : public INrClientTransportEventSink
        {
        public:
            [[nodiscard]] psnr::core::NrStatus PublishTransportConnected() noexcept override
            {
                kind = NrRecordedClientEventKind::TransportConnected;
                return psnr::core::NrStatus::Success();
            }

            [[nodiscard]] psnr::core::NrStatus PublishTransportConnectionFailed(
                const psnr::core::NrStatus transportStatus) noexcept override
            {
                kind = NrRecordedClientEventKind::TransportConnectionFailed;
                status = transportStatus;
                return psnr::core::NrStatus::Success();
            }

            [[nodiscard]] psnr::core::NrStatus PublishPacketReceived(
                const psnr::core::NrPacketType packetType, const std::span<const std::byte> payload) noexcept override
            {
                kind = NrRecordedClientEventKind::PacketReceived;
                recordedPacketType = packetType;
                payloadLength = payload.size();
                firstPayloadByte = payload.empty() ? std::byte{} : payload.front();
                return psnr::core::NrStatus::Success();
            }

            [[nodiscard]] psnr::core::NrStatus PublishTransportDisconnected(
                const NrClientDisconnectReason reason, const psnr::core::NrStatus transportStatus) noexcept override
            {
                kind = NrRecordedClientEventKind::TransportDisconnected;
                disconnectReason = reason;
                status = transportStatus;
                return psnr::core::NrStatus::Success();
            }

            [[nodiscard]] psnr::core::NrStatus HandleEventSpaceAvailable() noexcept override
            {
                kind = NrRecordedClientEventKind::PendingPromotion;
                return psnr::core::NrStatus::Success();
            }

            NrRecordedClientEventKind kind = NrRecordedClientEventKind::None;
            psnr::core::NrPacketType recordedPacketType{};
            std::size_t payloadLength = 0;
            std::byte firstPayloadByte{};
            NrClientDisconnectReason disconnectReason = NrClientDisconnectReason::None;
            psnr::core::NrStatus status;
        };

        TEST(NrClientTransportEventSinkTests, ContractIsInternalNonCopyablePolymorphicSeam)
        {
            EXPECT_TRUE(std::is_abstract_v<INrClientTransportEventSink>);
            EXPECT_FALSE(std::is_copy_constructible_v<INrClientTransportEventSink>);
            EXPECT_FALSE(std::is_copy_assignable_v<INrClientTransportEventSink>);
            EXPECT_TRUE(std::has_virtual_destructor_v<INrClientTransportEventSink>);
        }

        TEST(NrClientTransportEventSinkTests, FakeAdapterReceivesTypedSemanticEvents)
        {
            NrRecordingClientTransportEventSink recordingSink;
            INrClientTransportEventSink& sink = recordingSink;

            ASSERT_TRUE(sink.PublishTransportConnected().Succeeded());
            EXPECT_EQ(recordingSink.kind, NrRecordedClientEventKind::TransportConnected);

            const psnr::core::NrStatus connectFailure =
                psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::IoFailed, 10061);
            ASSERT_TRUE(sink.PublishTransportConnectionFailed(connectFailure).Succeeded());
            EXPECT_EQ(recordingSink.kind, NrRecordedClientEventKind::TransportConnectionFailed);
            EXPECT_EQ(recordingSink.status.ErrorCode(), psnr::core::NrErrorCode::IoFailed);
            EXPECT_EQ(recordingSink.status.NativeErrorCode(), 10061u);

            const std::array<std::byte, 2> payload = {std::byte{7}, std::byte{9}};
            ASSERT_TRUE(sink.PublishPacketReceived(psnr::core::NrPacketType{42}, payload).Succeeded());
            EXPECT_EQ(recordingSink.kind, NrRecordedClientEventKind::PacketReceived);
            EXPECT_EQ(recordingSink.recordedPacketType.value, 42u);
            EXPECT_EQ(recordingSink.payloadLength, 2u);
            EXPECT_EQ(std::to_integer<unsigned int>(recordingSink.firstPayloadByte), 7u);

            ASSERT_TRUE(sink.PublishTransportDisconnected(
                                NrClientDisconnectReason::RemoteClosed, psnr::core::NrStatus::Success())
                            .Succeeded());
            EXPECT_EQ(recordingSink.kind, NrRecordedClientEventKind::TransportDisconnected);
            EXPECT_EQ(recordingSink.disconnectReason, NrClientDisconnectReason::RemoteClosed);

            ASSERT_TRUE(sink.HandleEventSpaceAvailable().Succeeded());
            EXPECT_EQ(recordingSink.kind, NrRecordedClientEventKind::PendingPromotion);
        }
    } // namespace
} // namespace psnr::runtime::internal
