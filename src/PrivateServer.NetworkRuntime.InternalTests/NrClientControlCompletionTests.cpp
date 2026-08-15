#include "pch.h"

#include "NrClientControlCompletion.h"

#include "NrErrorCode.h"
#include "NrIocpCompletionPacket.h"
#include "NrIocpPort.h"

namespace psnr::runtime::internal
{
    namespace
    {
        TEST(NrClientControlCompletionTests, EventSpaceAvailablePostsTypedIocpControlCompletion)
        {
            NrIocpPort iocpPort;
            ASSERT_TRUE(iocpPort.Create().Succeeded());

            ASSERT_TRUE(
                PostClientControlCompletion(iocpPort, NrClientControlCompletionKind::EventSpaceAvailable).Succeeded());

            NrIocpCompletionPacket packet;
            ASSERT_TRUE(iocpPort.WaitForCompletion(packet).Succeeded());
            EXPECT_EQ(packet.overlapped, nullptr);
            EXPECT_EQ(packet.bytesTransferred, 0u);

            NrClientControlCompletionKind kind = NrClientControlCompletionKind::None;
            ASSERT_TRUE(DecodeClientControlCompletion(packet, kind).Succeeded());
            EXPECT_EQ(kind, NrClientControlCompletionKind::EventSpaceAvailable);
        }

        TEST(NrClientControlCompletionTests, StopPostsTypedIocpControlCompletion)
        {
            NrIocpPort iocpPort;
            ASSERT_TRUE(iocpPort.Create().Succeeded());

            ASSERT_TRUE(PostClientControlCompletion(iocpPort, NrClientControlCompletionKind::Stop).Succeeded());

            NrIocpCompletionPacket packet;
            ASSERT_TRUE(iocpPort.WaitForCompletion(packet).Succeeded());

            NrClientControlCompletionKind kind = NrClientControlCompletionKind::None;
            ASSERT_TRUE(DecodeClientControlCompletion(packet, kind).Succeeded());
            EXPECT_EQ(kind, NrClientControlCompletionKind::Stop);
        }

        TEST(NrClientControlCompletionTests, CommandsReadyPostsTypedIocpControlCompletion)
        {
            NrIocpPort iocpPort;
            ASSERT_TRUE(iocpPort.Create().Succeeded());

            ASSERT_TRUE(
                PostClientControlCompletion(iocpPort, NrClientControlCompletionKind::CommandsReady).Succeeded());

            NrIocpCompletionPacket packet;
            ASSERT_TRUE(iocpPort.WaitForCompletion(packet).Succeeded());

            NrClientControlCompletionKind kind = NrClientControlCompletionKind::None;
            ASSERT_TRUE(DecodeClientControlCompletion(packet, kind).Succeeded());
            EXPECT_EQ(kind, NrClientControlCompletionKind::CommandsReady);
        }

        TEST(NrClientControlCompletionTests, InvalidKindAndIoPacketAreRejectedWithoutChangingOutput)
        {
            NrIocpPort iocpPort;
            ASSERT_TRUE(iocpPort.Create().Succeeded());
            EXPECT_EQ(PostClientControlCompletion(iocpPort, NrClientControlCompletionKind::None).ErrorCode(),
                      psnr::core::NrErrorCode::InvalidArgument);

            NrClientControlCompletionKind kind = NrClientControlCompletionKind::EventSpaceAvailable;
            NrIocpCompletionPacket unknownPacket;
            unknownPacket.completionKey = 99;
            EXPECT_EQ(DecodeClientControlCompletion(unknownPacket, kind).ErrorCode(),
                      psnr::core::NrErrorCode::InvalidArgument);
            EXPECT_EQ(kind, NrClientControlCompletionKind::EventSpaceAvailable);

            OVERLAPPED overlapped{};
            NrIocpCompletionPacket ioPacket;
            ioPacket.completionKey = static_cast<std::uintptr_t>(NrClientControlCompletionKind::EventSpaceAvailable);
            ioPacket.overlapped = &overlapped;
            EXPECT_EQ(DecodeClientControlCompletion(ioPacket, kind).ErrorCode(),
                      psnr::core::NrErrorCode::InvalidArgument);
            EXPECT_EQ(kind, NrClientControlCompletionKind::EventSpaceAvailable);
        }
    } // namespace
} // namespace psnr::runtime::internal
