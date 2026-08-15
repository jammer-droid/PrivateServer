#include "pch.h"

#include "NrClientCommandChannel.h"

#include "NrClientControlCompletion.h"
#include "NrConcurrency.h"
#include "NrErrorCode.h"
#include "NrIocpCompletionPacket.h"
#include "NrIocpPort.h"
#include "NrMemoryPoolTestUtils.h"
#include "NrPacketHeader.h"
#include "NrPacketType.h"

#include <array>
#include <memory>

namespace psnr::runtime::internal
{
    namespace
    {
        constexpr std::size_t CommandChannelSendCapacity = 4;

        [[nodiscard]] std::unique_ptr<psnr::core::NrMemoryPoolManager> CreateCommandChannelMemoryPool()
        {
            psnr::core::NrMemoryPoolManagerConfig config = psnr::core::test::MakeDefaultMemoryPoolManagerConfig();
            const psnr::core::NrResult<std::size_t> queueStorageBytesResult =
                NrClientPendingSendQueue::RequiredStorageBytes(CommandChannelSendCapacity);
            EXPECT_TRUE(queueStorageBytesResult.Succeeded());
            if (queueStorageBytesResult.Failed())
            {
                return nullptr;
            }

            EXPECT_TRUE(psnr::core::test::SetPoolConfig(
                config, psnr::core::NrMemoryPoolRole::ClientPayloadQueueStorage,
                psnr::core::test::MakePoolConfig(queueStorageBytesResult.Value(), 1,
                                                 psnr::core::NrCacheLineSize)));
            EXPECT_TRUE(psnr::core::test::SetPoolConfig(
                config, psnr::core::NrMemoryPoolRole::Payload64,
                psnr::core::test::MakePoolConfig(64, CommandChannelSendCapacity)));
            EXPECT_TRUE(psnr::core::test::SetPoolConfig(
                config, psnr::core::NrMemoryPoolRole::PayloadRefControl,
                psnr::core::test::MakePoolConfig(64, CommandChannelSendCapacity)));
            return psnr::core::test::CreateMemoryPoolManager(config);
        }

        [[nodiscard]] std::unique_ptr<NrClientPendingSendQueue> CreateCommandChannelSendQueue(
            psnr::core::NrMemoryPoolManager& memoryPoolManager)
        {
            psnr::core::NrResult<std::unique_ptr<NrClientPendingSendQueue>> queueResult =
                NrClientPendingSendQueue::Create(memoryPoolManager,
                                                 psnr::core::NrMemoryPoolRole::ClientPayloadQueueStorage,
                                                 CommandChannelSendCapacity);
            EXPECT_TRUE(queueResult.Succeeded());
            return queueResult.Failed() ? nullptr : queueResult.TakeValue();
        }

        void CountCommandsReadyWakesBeforeMarker(NrIocpPort& iocpPort, std::size_t& outWakeCount)
        {
            outWakeCount = 0;
            ASSERT_TRUE(PostClientControlCompletion(iocpPort, NrClientControlCompletionKind::Stop).Succeeded());

            for (;;)
            {
                NrIocpCompletionPacket packet;
                ASSERT_TRUE(iocpPort.WaitForCompletion(packet).Succeeded());

                NrClientControlCompletionKind kind = NrClientControlCompletionKind::None;
                ASSERT_TRUE(DecodeClientControlCompletion(packet, kind).Succeeded());
                if (kind == NrClientControlCompletionKind::Stop)
                {
                    return;
                }

                ASSERT_EQ(kind, NrClientControlCompletionKind::CommandsReady);
                ++outWakeCount;
            }
        }

        void ConnectCommandChannel(NrClientCommandChannel& channel, NrIocpPort& iocpPort,
                                   std::uint64_t& outGeneration)
        {
            NrEndpoint endpoint;
            endpoint.port = 7777;
            ASSERT_TRUE(channel.SubmitConnect(endpoint, outGeneration).Succeeded());
            EXPECT_EQ(channel.ScheduleView(), psnr::core::NrActorScheduleView::Scheduled);
            std::size_t wakeCount = 0;
            CountCommandsReadyWakesBeforeMarker(iocpPort, wakeCount);
            EXPECT_EQ(wakeCount, 1u);
            ASSERT_TRUE(channel.TryBeginDrain());

            NrClientCommand command;
            ASSERT_TRUE(channel.TryPopCommand(command).Succeeded());
            ASSERT_EQ(command.kind, NrClientCommandKind::Connect);
            ASSERT_EQ(command.attemptGeneration, outGeneration);
            ASSERT_TRUE(channel.CompleteDrain(false).Succeeded());
            EXPECT_EQ(channel.ScheduleView(), psnr::core::NrActorScheduleView::Idle);
            ASSERT_TRUE(channel.RecordConnectSucceeded(outGeneration).Succeeded());
        }

        TEST(NrClientCommandChannelTests, LifecycleCommandsCommitInOwnerOrderAndGateSendAdmission)
        {
            std::unique_ptr<psnr::core::NrMemoryPoolManager> memoryPoolManager = CreateCommandChannelMemoryPool();
            ASSERT_NE(memoryPoolManager, nullptr);
            std::unique_ptr<NrClientPendingSendQueue> pendingSendQueue =
                CreateCommandChannelSendQueue(*memoryPoolManager);
            ASSERT_NE(pendingSendQueue, nullptr);

            NrIocpPort iocpPort;
            ASSERT_TRUE(iocpPort.Create().Succeeded());
            NrClientCommandChannel channel(iocpPort, *memoryPoolManager, *pendingSendQueue);

            NrEndpoint endpoint;
            endpoint.ipv4Address = NrIPv4Address{10, 20, 30, 40};
            endpoint.port = 7777;
            std::uint64_t generation = 0;
            ASSERT_TRUE(channel.SubmitConnect(endpoint, generation).Succeeded());
            EXPECT_EQ(generation, 1u);
            EXPECT_EQ(channel.State(), NrClientLifecycleState::TransportConnecting);

            ASSERT_TRUE(channel.SubmitDisconnect().Succeeded());
            EXPECT_EQ(channel.State(), NrClientLifecycleState::TransportDisconnecting);
            EXPECT_EQ(channel.ScheduleView(), psnr::core::NrActorScheduleView::Scheduled);

            std::size_t wakeCount = 0;
            CountCommandsReadyWakesBeforeMarker(iocpPort, wakeCount);
            EXPECT_EQ(wakeCount, 1u);
            ASSERT_TRUE(channel.TryBeginDrain());

            NrClientCommand command;
            ASSERT_TRUE(channel.TryPopCommand(command).Succeeded());
            EXPECT_EQ(command.kind, NrClientCommandKind::Connect);
            EXPECT_EQ(command.attemptGeneration, generation);
            EXPECT_EQ(command.remoteEndpoint.ipv4Address.octets[0], 10u);
            EXPECT_EQ(command.remoteEndpoint.port, 7777u);

            command = NrClientCommand{};
            ASSERT_TRUE(channel.TryPopCommand(command).Succeeded());
            EXPECT_EQ(command.kind, NrClientCommandKind::Disconnect);
            EXPECT_EQ(command.attemptGeneration, generation);
            ASSERT_TRUE(channel.CompleteDrain(false).Succeeded());
            EXPECT_EQ(channel.ScheduleView(), psnr::core::NrActorScheduleView::Idle);

            EXPECT_TRUE(channel.RecordConnectSucceeded(generation).Succeeded());
            EXPECT_EQ(channel.State(), NrClientLifecycleState::TransportDisconnecting);
            const std::array<std::byte, 1> payload{std::byte{0x11}};
            EXPECT_EQ(channel.SubmitSend(psnr::core::NrPacketType{1}, payload).ErrorCode(),
                      psnr::core::NrErrorCode::InvalidState);

            ASSERT_TRUE(channel.RecordDisconnected(generation).Succeeded());
            EXPECT_EQ(channel.State(), NrClientLifecycleState::Idle);

            ASSERT_TRUE(channel.SignalEventSpaceAvailable().Succeeded());
            ASSERT_TRUE(channel.SignalEventSpaceAvailable().Succeeded());
            CountCommandsReadyWakesBeforeMarker(iocpPort, wakeCount);
            EXPECT_EQ(wakeCount, 1u);
            ASSERT_TRUE(channel.TryBeginDrain());
            command = NrClientCommand{};
            ASSERT_TRUE(channel.TryPopCommand(command).Succeeded());
            EXPECT_EQ(command.kind, NrClientCommandKind::EventSpaceAvailable);
            ASSERT_TRUE(channel.CompleteDrain(false).Succeeded());

            ASSERT_TRUE(channel.SubmitShutdown().Succeeded());
            EXPECT_EQ(channel.State(), NrClientLifecycleState::Shutdown);
            EXPECT_EQ(channel.SignalEventSpaceAvailable().ErrorCode(), psnr::core::NrErrorCode::InvalidState);
            CountCommandsReadyWakesBeforeMarker(iocpPort, wakeCount);
            EXPECT_EQ(wakeCount, 1u);
            ASSERT_TRUE(channel.TryBeginDrain());

            command = NrClientCommand{};
            ASSERT_TRUE(channel.TryPopCommand(command).Succeeded());
            EXPECT_EQ(command.kind, NrClientCommandKind::Shutdown);
            command = NrClientCommand{};
            EXPECT_EQ(channel.TryPopCommand(command).ErrorCode(), psnr::core::NrErrorCode::QueueEmpty);
            ASSERT_TRUE(channel.CompleteDrain(false).Succeeded());
            EXPECT_EQ(channel.ScheduleView(), psnr::core::NrActorScheduleView::Idle);
        }

        TEST(NrClientCommandChannelTests, SendAdmissionBoundsActiveAndQueuedOwnershipTogether)
        {
            std::unique_ptr<psnr::core::NrMemoryPoolManager> memoryPoolManager = CreateCommandChannelMemoryPool();
            ASSERT_NE(memoryPoolManager, nullptr);
            std::unique_ptr<NrClientPendingSendQueue> pendingSendQueue =
                CreateCommandChannelSendQueue(*memoryPoolManager);
            ASSERT_NE(pendingSendQueue, nullptr);

            NrIocpPort iocpPort;
            ASSERT_TRUE(iocpPort.Create().Succeeded());
            NrClientCommandChannel channel(iocpPort, *memoryPoolManager, *pendingSendQueue);
            std::uint64_t generation = 0;
            ConnectCommandChannel(channel, iocpPort, generation);

            std::array<std::byte, 3> callerPayload{std::byte{0x10}, std::byte{0x20}, std::byte{0x30}};
            for (std::size_t index = 0; index < CommandChannelSendCapacity; ++index)
            {
                ASSERT_TRUE(channel.SubmitSend(
                    psnr::core::NrPacketType{static_cast<std::uint16_t>(100 + index)}, callerPayload).Succeeded());
            }

            callerPayload.fill(std::byte{0x7F});
            EXPECT_EQ(channel.SendPipelineDepth(), CommandChannelSendCapacity);
            EXPECT_EQ(channel.PendingSendQueueDepth(), CommandChannelSendCapacity);
            EXPECT_EQ(channel.PendingSendQueueHighWatermark(), CommandChannelSendCapacity);
            EXPECT_EQ(channel.SubmitSend(psnr::core::NrPacketType{200}, callerPayload).ErrorCode(),
                      psnr::core::NrErrorCode::QueueFull);
            EXPECT_EQ(channel.ScheduleView(), psnr::core::NrActorScheduleView::Scheduled);

            std::size_t wakeCount = 0;
            CountCommandsReadyWakesBeforeMarker(iocpPort, wakeCount);
            EXPECT_EQ(wakeCount, 1u);
            ASSERT_TRUE(channel.TryBeginDrain());
            NrClientPendingSend acceptedSend;
            ASSERT_TRUE(channel.TryPopSend(acceptedSend).Succeeded());
            ASSERT_EQ(acceptedSend.attemptGeneration, generation);
            const std::span<const std::byte> framedBytes = acceptedSend.payload.Bytes();
            ASSERT_EQ(framedBytes.size(), psnr::core::NrPacketHeaderLength + 3);
            EXPECT_EQ(framedBytes[psnr::core::NrPacketHeaderLength], std::byte{0x10});
            EXPECT_EQ(framedBytes[psnr::core::NrPacketHeaderLength + 1], std::byte{0x20});
            EXPECT_EQ(framedBytes[psnr::core::NrPacketHeaderLength + 2], std::byte{0x30});
            EXPECT_EQ(channel.SendPipelineDepth(), CommandChannelSendCapacity);
            EXPECT_EQ(channel.PendingSendQueueDepth(), CommandChannelSendCapacity - 1);
            ASSERT_TRUE(channel.CompleteDrain(true).Succeeded());
            EXPECT_EQ(channel.ScheduleView(), psnr::core::NrActorScheduleView::Scheduled);

            acceptedSend = NrClientPendingSend{};
            ASSERT_TRUE(channel.CompleteAcceptedSend().Succeeded());
            ASSERT_TRUE(channel.SubmitSend(psnr::core::NrPacketType{201}, callerPayload).Succeeded());
            EXPECT_EQ(channel.SendPipelineDepth(), CommandChannelSendCapacity);
            EXPECT_EQ(channel.PendingSendQueueDepth(), CommandChannelSendCapacity);

            CountCommandsReadyWakesBeforeMarker(iocpPort, wakeCount);
            EXPECT_EQ(wakeCount, 1u);
            ASSERT_TRUE(channel.TryBeginDrain());
            ASSERT_TRUE(channel.DiscardPendingSends().Succeeded());
            ASSERT_TRUE(channel.CompleteDrain(false).Succeeded());
            EXPECT_EQ(channel.ScheduleView(), psnr::core::NrActorScheduleView::Idle);
            EXPECT_EQ(channel.SendPipelineDepth(), 0u);
            EXPECT_EQ(channel.PendingSendQueueDepth(), 0u);
            EXPECT_EQ(psnr::core::test::Stats(*memoryPoolManager, psnr::core::NrMemoryPoolRole::Payload64).inUse,
                      0u);
            EXPECT_EQ(
                psnr::core::test::Stats(*memoryPoolManager, psnr::core::NrMemoryPoolRole::PayloadRefControl).inUse,
                0u);
        }

        TEST(NrClientCommandChannelTests, InvalidPortRejectsBeforeAdmissionAndOwnershipPublication)
        {
            std::unique_ptr<psnr::core::NrMemoryPoolManager> memoryPoolManager = CreateCommandChannelMemoryPool();
            ASSERT_NE(memoryPoolManager, nullptr);
            std::unique_ptr<NrClientPendingSendQueue> pendingSendQueue =
                CreateCommandChannelSendQueue(*memoryPoolManager);
            ASSERT_NE(pendingSendQueue, nullptr);

            NrIocpPort iocpPort;
            NrClientCommandChannel channel(iocpPort, *memoryPoolManager, *pendingSendQueue);

            NrEndpoint endpoint;
            endpoint.port = 7777;
            std::uint64_t generation = 99;
            EXPECT_EQ(channel.SubmitConnect(endpoint, generation).ErrorCode(),
                      psnr::core::NrErrorCode::InvalidState);
            EXPECT_EQ(generation, 99u);
            EXPECT_EQ(channel.CurrentGeneration(), 0u);
            EXPECT_EQ(channel.State(), NrClientLifecycleState::Idle);
            NrClientCommand command;
            EXPECT_EQ(channel.TryPopCommand(command).ErrorCode(), psnr::core::NrErrorCode::QueueEmpty);

            ASSERT_TRUE(iocpPort.Create().Succeeded());
            generation = 0;
            ConnectCommandChannel(channel, iocpPort, generation);
            ASSERT_TRUE(iocpPort.Close().Succeeded());

            const std::array<std::byte, 2> payload{std::byte{0x44}, std::byte{0x55}};
            EXPECT_EQ(channel.SubmitSend(psnr::core::NrPacketType{300}, payload).ErrorCode(),
                      psnr::core::NrErrorCode::InvalidState);
            EXPECT_EQ(channel.SendPipelineDepth(), 0u);
            EXPECT_EQ(channel.PendingSendQueueDepth(), 0u);
            EXPECT_EQ(psnr::core::test::Stats(*memoryPoolManager, psnr::core::NrMemoryPoolRole::Payload64).inUse,
                      0u);
            EXPECT_EQ(
                psnr::core::test::Stats(*memoryPoolManager, psnr::core::NrMemoryPoolRole::PayloadRefControl).inUse,
                0u);

            EXPECT_EQ(channel.SubmitDisconnect().ErrorCode(), psnr::core::NrErrorCode::InvalidState);
            EXPECT_EQ(channel.State(), NrClientLifecycleState::TransportConnected);
            EXPECT_FALSE(channel.HasWakeFailure());
            command = NrClientCommand{};
            EXPECT_EQ(channel.TryPopCommand(command).ErrorCode(), psnr::core::NrErrorCode::QueueEmpty);
        }
    } // namespace
} // namespace psnr::runtime::internal
