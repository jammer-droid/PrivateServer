#include "pch.h"

#include "NrClientConnectIoContext.h"
#include "NrClientIoCompletionDispatcher.h"
#include "NrErrorCode.h"
#include "NrIocpIoContextHeader.h"
#include "NrRecvIoContext.h"
#include "NrSendIoContext.h"
#include "NrSocketAddressWin32.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace psnr::runtime::internal
{
    namespace
    {
        enum class NrRecordedClientCompletionKind : std::uint8_t
        {
            None,
            Control,
            Connect,
            Recv,
            Send,
            Stale,
        };

        class NrRecordingClientIoCompletionTarget final : public INrClientIoCompletionTarget
        {
        public:
            [[nodiscard]] std::uint64_t CurrentAttemptGeneration() const noexcept override
            {
                return currentAttemptGeneration;
            }

            [[nodiscard]] psnr::core::NrStatus HandleClientControlCompletion(
                const NrClientControlCompletionKind kind) noexcept override
            {
                recordedKind = NrRecordedClientCompletionKind::Control;
                controlKind = kind;
                return returnStatus;
            }

            [[nodiscard]] psnr::core::NrStatus HandleConnectCompletion(
                NrClientConnectIoContext& context, const NrIocpCompletionPacket& packet) noexcept override
            {
                recordedKind = NrRecordedClientCompletionKind::Connect;
                recordedContext = &context;
                recordedPacket = packet;
                return returnStatus;
            }

            [[nodiscard]] psnr::core::NrStatus HandleRecvCompletion(
                NrRecvIoContext& context, const NrIocpCompletionPacket& packet) noexcept override
            {
                recordedKind = NrRecordedClientCompletionKind::Recv;
                recordedContext = &context;
                recordedPacket = packet;
                return returnStatus;
            }

            [[nodiscard]] psnr::core::NrStatus HandleSendCompletion(
                NrSendIoContext& context, const NrIocpCompletionPacket& packet) noexcept override
            {
                recordedKind = NrRecordedClientCompletionKind::Send;
                recordedContext = &context;
                recordedPacket = packet;
                return returnStatus;
            }

            [[nodiscard]] psnr::core::NrStatus HandleStaleIoCompletion(
                const NrIoOperationType operationType, const std::uint64_t attemptGeneration,
                const NrIocpCompletionPacket& packet) noexcept override
            {
                recordedKind = NrRecordedClientCompletionKind::Stale;
                staleOperationType = operationType;
                staleAttemptGeneration = attemptGeneration;
                recordedContext = packet.overlapped;
                recordedPacket = packet;
                return returnStatus;
            }

            void Reset() noexcept
            {
                recordedKind = NrRecordedClientCompletionKind::None;
                controlKind = NrClientControlCompletionKind::None;
                staleOperationType = NrIoOperationType::Unknown;
                staleAttemptGeneration = 0;
                recordedContext = nullptr;
                recordedPacket = NrIocpCompletionPacket{};
            }

            std::uint64_t currentAttemptGeneration = 7;
            psnr::core::NrStatus returnStatus = psnr::core::NrStatus::Success();
            NrRecordedClientCompletionKind recordedKind = NrRecordedClientCompletionKind::None;
            NrClientControlCompletionKind controlKind = NrClientControlCompletionKind::None;
            NrIoOperationType staleOperationType = NrIoOperationType::Unknown;
            std::uint64_t staleAttemptGeneration = 0;
            const void* recordedContext = nullptr;
            NrIocpCompletionPacket recordedPacket;
        };

        TEST(NrClientIoCompletionDispatcherTests, CurrentGenerationRoutesConnectRecvAndSendDirectly)
        {
            NrRecordingClientIoCompletionTarget target;
            NrClientIoCompletionDispatcher dispatcher(target);

            NrSocketAddressWin32 remoteAddress;
            NrClientConnectIoContext connectContext(7, remoteAddress);
            NrIocpCompletionPacket connectPacket;
            connectPacket.bytesTransferred = 1;
            connectPacket.overlapped = connectContext.Overlapped();
            ASSERT_TRUE(dispatcher.HandleIoCompletion(connectPacket).Succeeded());
            EXPECT_EQ(target.recordedKind, NrRecordedClientCompletionKind::Connect);
            EXPECT_EQ(target.recordedContext, &connectContext);
            EXPECT_EQ(target.recordedPacket.bytesTransferred, 1u);

            target.Reset();
            std::array<std::byte, 8> recvBytes{};
            NrRecvIoContext recvContext(7, std::span(recvBytes));
            NrIocpCompletionPacket recvPacket;
            recvPacket.bytesTransferred = 4;
            recvPacket.overlapped = recvContext.header.Overlapped();
            ASSERT_TRUE(dispatcher.HandleIoCompletion(recvPacket).Succeeded());
            EXPECT_EQ(target.recordedKind, NrRecordedClientCompletionKind::Recv);
            EXPECT_EQ(target.recordedContext, &recvContext);
            EXPECT_EQ(target.recordedPacket.bytesTransferred, 4u);

            target.Reset();
            NrSendIoContext sendContext(7, psnr::core::NrPayloadRef{});
            NrIocpCompletionPacket sendPacket;
            sendPacket.bytesTransferred = 3;
            sendPacket.overlapped = sendContext.header.Overlapped();
            ASSERT_TRUE(dispatcher.HandleIoCompletion(sendPacket).Succeeded());
            EXPECT_EQ(target.recordedKind, NrRecordedClientCompletionKind::Send);
            EXPECT_EQ(target.recordedContext, &sendContext);
            EXPECT_EQ(target.recordedPacket.bytesTransferred, 3u);
        }

        TEST(NrClientIoCompletionDispatcherTests, StaleGenerationUsesCleanupRouteWithoutCurrentHandler)
        {
            NrRecordingClientIoCompletionTarget target;
            NrClientIoCompletionDispatcher dispatcher(target);
            std::array<std::byte, 8> recvBytes{};
            NrRecvIoContext recvContext(6, std::span(recvBytes));
            NrIocpCompletionPacket packet;
            packet.overlapped = recvContext.header.Overlapped();

            ASSERT_TRUE(dispatcher.HandleIoCompletion(packet).Succeeded());
            EXPECT_EQ(target.recordedKind, NrRecordedClientCompletionKind::Stale);
            EXPECT_EQ(target.staleOperationType, NrIoOperationType::Recv);
            EXPECT_EQ(target.staleAttemptGeneration, 6u);
            EXPECT_EQ(target.recordedContext, packet.overlapped);
        }

        TEST(NrClientIoCompletionDispatcherTests, TypedControlCompletionRoutesToClientTarget)
        {
            NrRecordingClientIoCompletionTarget target;
            NrClientIoCompletionDispatcher dispatcher(target);
            NrIocpCompletionPacket packet;
            packet.completionKey = static_cast<std::uintptr_t>(NrClientControlCompletionKind::CommandsReady);

            ASSERT_TRUE(dispatcher.HandleControlCompletion(packet).Succeeded());
            EXPECT_EQ(target.recordedKind, NrRecordedClientCompletionKind::Control);
            EXPECT_EQ(target.controlKind, NrClientControlCompletionKind::CommandsReady);
        }

        TEST(NrClientIoCompletionDispatcherTests, InvalidOrServerOnlyCompletionIsRejected)
        {
            NrRecordingClientIoCompletionTarget target;
            NrClientIoCompletionDispatcher dispatcher(target);

            NrIocpCompletionPacket nullIoPacket;
            EXPECT_EQ(dispatcher.HandleIoCompletion(nullIoPacket).ErrorCode(),
                      psnr::core::NrErrorCode::InvalidArgument);

            NrIocpIoContextHeader acceptHeader;
            acceptHeader.Reset(NrIoOperationType::Accept);
            NrIocpCompletionPacket acceptPacket;
            acceptPacket.overlapped = acceptHeader.Overlapped();
            EXPECT_EQ(dispatcher.HandleIoCompletion(acceptPacket).ErrorCode(),
                      psnr::core::NrErrorCode::InvalidArgument);

            NrIocpCompletionPacket unknownControlPacket;
            unknownControlPacket.completionKey = 99;
            EXPECT_EQ(dispatcher.HandleControlCompletion(unknownControlPacket).ErrorCode(),
                      psnr::core::NrErrorCode::InvalidArgument);
            EXPECT_EQ(target.recordedKind, NrRecordedClientCompletionKind::None);
        }
    } // namespace
} // namespace psnr::runtime::internal
