#include "pch.h"

#include <PrivateServer/NetworkRuntime/NrServer.h>
#include "NrServerTestUtils.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <thread>
#include <type_traits>
#include <utility>

namespace psnr::runtime
{
    using psnr::core::NrErrorCode;
    using psnr::core::NrStatus;
    using tests::CreateServer;
    using tests::CreateServerConfig;
    using tests::ExpectStatus;

    namespace
    {
        TEST(NrServerLifecycleTests, ServerPublicShellIsMoveOnly)
        {
            EXPECT_FALSE(std::is_copy_constructible_v<NrServer>);
            EXPECT_FALSE(std::is_copy_assignable_v<NrServer>);
            EXPECT_TRUE(std::is_nothrow_move_constructible_v<NrServer>);
            EXPECT_TRUE(std::is_nothrow_move_assignable_v<NrServer>);
        }

        TEST(NrServerLifecycleTests, DefaultConstructedServerIsInvalid)
        {
            NrServer server;
            NrToWorldEvent event;

            EXPECT_FALSE(server.IsValid());
            ExpectStatus(server.Start(), NrStatus::Failure(NrErrorCode::InvalidState));
            ExpectStatus(server.RequestSessionClose(1, NrSessionCloseRequestReason::ApplicationRequested),
                         NrStatus::Failure(NrErrorCode::InvalidState));
            ExpectStatus(server.TryPopToWorldEvent(&event), NrStatus::Failure(NrErrorCode::InvalidState));

            std::array<NrToWorldEvent, 1> eventBuffer;
            std::size_t eventCount = 99;
            ExpectStatus(server.TryPopToWorldEvents(eventBuffer.data(), eventBuffer.size(), &eventCount),
                         NrStatus::Failure(NrErrorCode::InvalidState));
            EXPECT_EQ(eventCount, 99u);
        }

        TEST(NrServerLifecycleTests, OutputPointersRejectNull)
        {
            const NrServerConfig config = CreateServerConfig();
            NrServer server;

            ExpectStatus(NrServer::Create(config, nullptr), NrStatus::Failure(NrErrorCode::InvalidArgument));
            ExpectStatus(server.TryPopToWorldEvent(nullptr), NrStatus::Failure(NrErrorCode::InvalidArgument));

            std::array<NrToWorldEvent, 1> eventBuffer;
            std::size_t eventCount = 99;
            ExpectStatus(server.TryPopToWorldEvents(nullptr, eventBuffer.size(), &eventCount),
                         NrStatus::Failure(NrErrorCode::InvalidArgument));
            ExpectStatus(server.TryPopToWorldEvents(eventBuffer.data(), 0, &eventCount),
                         NrStatus::Failure(NrErrorCode::InvalidArgument));
            ExpectStatus(server.TryPopToWorldEvents(eventBuffer.data(), eventBuffer.size(), nullptr),
                         NrStatus::Failure(NrErrorCode::InvalidArgument));
            ExpectStatus(server.WaitForToWorldEvents(0, nullptr), NrStatus::Failure(NrErrorCode::InvalidArgument));
            EXPECT_EQ(eventCount, 99u);
        }

        TEST(NrServerLifecycleTests, ValidServerReportsEmptyToWorldHandoffWithoutBlocking)
        {
            NrServer server = CreateServer();
            NrToWorldEvent event;

            ExpectStatus(server.TryPopToWorldEvent(&event), NrStatus::Failure(NrErrorCode::QueueEmpty));
            EXPECT_FALSE(event.IsValid());

            std::array<NrToWorldEvent, 1> eventBuffer;
            std::size_t eventCount = 99;
            ExpectStatus(server.TryPopToWorldEvents(eventBuffer.data(), eventBuffer.size(), &eventCount),
                         NrStatus::Failure(NrErrorCode::QueueEmpty));
            EXPECT_EQ(eventCount, 99u);
            EXPECT_FALSE(eventBuffer[0].IsValid());
        }

        TEST(NrServerLifecycleTests, WaitForToWorldEventsDistinguishesTimeoutFromShutdown)
        {
            NrServer server = CreateServer();
            NrToWorldWaitResult waitResult = NrToWorldWaitResult::EventsAvailable;

            ExpectStatus(server.WaitForToWorldEvents(0, &waitResult), NrStatus::Success());
            EXPECT_EQ(waitResult, NrToWorldWaitResult::TimedOut);

            ExpectStatus(server.Shutdown(), NrStatus::Success());
            ExpectStatus(server.WaitForToWorldEvents(0, &waitResult), NrStatus::Success());
            EXPECT_EQ(waitResult, NrToWorldWaitResult::Closed);
        }

        TEST(NrServerLifecycleTests, ShutdownWakesConsumerBlockedOnToWorldEvents)
        {
            NrServer server = CreateServer();
            std::atomic<bool> waitStarted = false;
            NrToWorldWaitResult waitResult = NrToWorldWaitResult::TimedOut;
            NrStatus waitStatus = NrStatus::Failure(NrErrorCode::InvalidState);
            std::thread consumer(
                [&]() noexcept
                {
                    waitStarted.store(true, std::memory_order_release);
                    waitStatus = server.WaitForToWorldEvents(1000, &waitResult);
                });

            while (!waitStarted.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
            const NrStatus shutdownStatus = server.Shutdown();
            consumer.join();

            ExpectStatus(shutdownStatus, NrStatus::Success());
            ExpectStatus(waitStatus, NrStatus::Success());
            EXPECT_EQ(waitResult, NrToWorldWaitResult::Closed);
        }

        TEST(NrServerLifecycleTests, CreateBuildsValidServer)
        {
            NrServerConfig config = CreateServerConfig();

            NrServer server;

            ExpectStatus(NrServer::Create(config, &server), NrStatus::Success());

            EXPECT_TRUE(server.IsValid());
        }

        TEST(NrServerLifecycleTests, CreatedServerRejectsSessionCloseBeforeStart)
        {
            NrServer server = CreateServer();

            ExpectStatus(server.RequestSessionClose(1, NrSessionCloseRequestReason::ApplicationRequested),
                         NrStatus::Failure(NrErrorCode::InvalidState));
        }

        TEST(NrServerLifecycleTests, CreateRejectsAlreadyValidServer)
        {
            NrServerConfig config;
            config.bindEndpoint.port = 27015;

            NrServer server = CreateServer();

            const NrStatus status = NrServer::Create(config, &server);

            ExpectStatus(status, NrStatus::Failure(NrErrorCode::InvalidState));
        }

        TEST(NrServerLifecycleTests, CreateRejectsZeroPort)
        {
            NrServerConfig config;
            config.bindEndpoint.port = 0;
            NrServer server;

            const NrStatus status = NrServer::Create(config, &server);

            ExpectStatus(status, NrStatus::Failure(NrErrorCode::InvalidArgument));
            EXPECT_FALSE(server.IsValid());
        }

        TEST(NrServerLifecycleTests, CreateRejectsNonPositiveListenBacklog)
        {
            NrServerConfig config;
            config.bindEndpoint.port = 27015;
            config.listenBacklog = 0;
            NrServer server;

            const NrStatus status = NrServer::Create(config, &server);

            ExpectStatus(status, NrStatus::Failure(NrErrorCode::InvalidArgument));
            EXPECT_FALSE(server.IsValid());
        }

        TEST(NrServerLifecycleTests, CreateRejectsZeroAcceptSlotCount)
        {
            NrServerConfig config;
            config.bindEndpoint.port = 27015;
            config.acceptSlotCount = 0;
            NrServer server;

            const NrStatus status = NrServer::Create(config, &server);

            ExpectStatus(status, NrStatus::Failure(NrErrorCode::InvalidArgument));
            EXPECT_FALSE(server.IsValid());
        }

        TEST(NrServerLifecycleTests, CreateRejectsZeroActorMailboxCapacity)
        {
            NrServerConfig config = CreateServerConfig();
            config.actorMailboxCapacity = 0;
            NrServer server;

            const NrStatus status = NrServer::Create(config, &server);

            ExpectStatus(status, NrStatus::Failure(NrErrorCode::InvalidArgument));
            EXPECT_FALSE(server.IsValid());
        }

        TEST(NrServerLifecycleTests, CreateRejectsZeroPendingSendQueueCapacity)
        {
            NrServerConfig config = CreateServerConfig();
            config.pendingSendQueueCapacity = 0;
            NrServer server;

            const NrStatus status = NrServer::Create(config, &server);

            ExpectStatus(status, NrStatus::Failure(NrErrorCode::InvalidArgument));
            EXPECT_FALSE(server.IsValid());
        }

        TEST(NrServerLifecycleTests, CreateRejectsZeroPayloadPoolBlockCount)
        {
            NrServerConfig config = CreateServerConfig();
            config.payloadPools.payloadRefControlBlockCount = 0;
            NrServer server;

            const NrStatus status = NrServer::Create(config, &server);

            ExpectStatus(status, NrStatus::Failure(NrErrorCode::InvalidArgument));
            EXPECT_FALSE(server.IsValid());
        }

        TEST(NrServerLifecycleTests, CreateAcceptsConfiguredWorldIngressPacketType)
        {
            constexpr NrPacketType BenchmarkRequestPacketType{0x7F01};
            const std::array<NrPacketType, 1> packetTypes = {BenchmarkRequestPacketType};
            NrServerConfig config = CreateServerConfig();
            config.additionalWorldIngressPacketTypes = NrPacketTypeView{
                packetTypes.data(),
                static_cast<std::uint32_t>(packetTypes.size()),
            };
            NrServer server;

            const NrStatus status = NrServer::Create(config, &server);

            ExpectStatus(status, NrStatus::Success());
            EXPECT_TRUE(server.IsValid());
        }

        TEST(NrServerLifecycleTests, CreateRejectsMissingWorldIngressPacketTypeData)
        {
            NrServerConfig config = CreateServerConfig();
            config.additionalWorldIngressPacketTypes = NrPacketTypeView{nullptr, 1};
            NrServer server;

            const NrStatus status = NrServer::Create(config, &server);

            ExpectStatus(status, NrStatus::Failure(NrErrorCode::InvalidArgument));
            EXPECT_FALSE(server.IsValid());
        }

        TEST(NrServerLifecycleTests, CreateRejectsDuplicateConfiguredWorldIngressPacketTypes)
        {
            constexpr NrPacketType BenchmarkRequestPacketType{0x7F01};
            const std::array<NrPacketType, 2> packetTypes = {
                BenchmarkRequestPacketType,
                BenchmarkRequestPacketType,
            };
            NrServerConfig config = CreateServerConfig();
            config.additionalWorldIngressPacketTypes = NrPacketTypeView{
                packetTypes.data(),
                static_cast<std::uint32_t>(packetTypes.size()),
            };
            NrServer server;

            const NrStatus status = NrServer::Create(config, &server);

            ExpectStatus(status, NrStatus::Failure(NrErrorCode::InvalidArgument));
            EXPECT_FALSE(server.IsValid());
        }

        TEST(NrServerLifecycleTests, CreateRejectsWorldIngressPacketTypeThatOverridesDefaultRoute)
        {
            constexpr NrPacketType HeartbeatPacketType{0};
            const std::array<NrPacketType, 1> packetTypes = {HeartbeatPacketType};
            NrServerConfig config = CreateServerConfig();
            config.additionalWorldIngressPacketTypes = NrPacketTypeView{
                packetTypes.data(),
                static_cast<std::uint32_t>(packetTypes.size()),
            };
            NrServer server;

            const NrStatus status = NrServer::Create(config, &server);

            ExpectStatus(status, NrStatus::Failure(NrErrorCode::InvalidArgument));
            EXPECT_FALSE(server.IsValid());
        }

        TEST(NrServerLifecycleTests, MoveConstructTransfersValidity)
        {
            NrServer source = CreateServer();

            NrServer target(std::move(source));

            EXPECT_FALSE(source.IsValid());
            EXPECT_TRUE(target.IsValid());
            ExpectStatus(target.Start(), NrStatus::Success());
        }

        TEST(NrServerLifecycleTests, MovedFromServerRejectsLifecycleCalls)
        {
            NrServer source = CreateServer();
            NrServer target(std::move(source));

            EXPECT_FALSE(source.IsValid());
            ExpectStatus(source.Start(), NrStatus::Failure(NrErrorCode::InvalidState));
            ExpectStatus(source.RequestStop(), NrStatus::Failure(NrErrorCode::InvalidState));
            ExpectStatus(source.Shutdown(), NrStatus::Failure(NrErrorCode::InvalidState));
        }

        TEST(NrServerLifecycleTests, MoveAssignTransfersValidity)
        {
            NrServer source = CreateServer();
            NrServer target;

            target = std::move(source);

            EXPECT_FALSE(source.IsValid());
            EXPECT_TRUE(target.IsValid());
            ExpectStatus(target.Start(), NrStatus::Success());
        }

        TEST(NrServerLifecycleTests, MoveAssignFromInvalidClearsTarget)
        {
            NrServer source;
            NrServer target = CreateServer();

            target = std::move(source);

            EXPECT_FALSE(source.IsValid());
            EXPECT_FALSE(target.IsValid());
            ExpectStatus(target.Start(), NrStatus::Failure(NrErrorCode::InvalidState));
        }

        TEST(NrServerLifecycleTests, ServerPublicShellStartsStopsAndShutsDown)
        {
            NrServer server = CreateServer();

            ExpectStatus(server.Start(), NrStatus::Success());
            ExpectStatus(server.RequestStop(), NrStatus::Success());
            ExpectStatus(server.Shutdown(), NrStatus::Success());
        }

        TEST(NrServerLifecycleTests, ServerCannotStartTwice)
        {
            NrServer server = CreateServer();

            ExpectStatus(server.Start(), NrStatus::Success());

            const NrStatus status = server.Start();

            ExpectStatus(status, NrStatus::Failure(NrErrorCode::InvalidState));
        }

        TEST(NrServerLifecycleTests, ServerCannotRequestStopBeforeStart)
        {
            NrServer server = CreateServer();

            const NrStatus status = server.RequestStop();

            ExpectStatus(status, NrStatus::Failure(NrErrorCode::InvalidState));
        }

        TEST(NrServerLifecycleTests, ServerRequestStopIsIdempotentAfterStart)
        {
            NrServer server = CreateServer();

            ExpectStatus(server.Start(), NrStatus::Success());
            ExpectStatus(server.RequestStop(), NrStatus::Success());

            ExpectStatus(server.RequestStop(), NrStatus::Success());
        }

        TEST(NrServerLifecycleTests, ServerCannotStartAfterRequestStop)
        {
            NrServer server = CreateServer();

            ExpectStatus(server.Start(), NrStatus::Success());
            ExpectStatus(server.RequestStop(), NrStatus::Success());

            const NrStatus status = server.Start();

            ExpectStatus(status, NrStatus::Failure(NrErrorCode::InvalidState));
        }

        TEST(NrServerLifecycleTests, ServerCannotStartAfterShutdown)
        {
            NrServer server = CreateServer();

            ExpectStatus(server.Shutdown(), NrStatus::Success());

            const NrStatus status = server.Start();

            ExpectStatus(status, NrStatus::Failure(NrErrorCode::InvalidState));
        }

        TEST(NrServerLifecycleTests, ServerCannotRequestStopAfterShutdown)
        {
            NrServer server = CreateServer();

            ExpectStatus(server.Shutdown(), NrStatus::Success());

            const NrStatus status = server.RequestStop();

            ExpectStatus(status, NrStatus::Failure(NrErrorCode::InvalidState));
        }

        TEST(NrServerLifecycleTests, ServerShutdownIsIdempotent)
        {
            NrServer server = CreateServer();

            ExpectStatus(server.Shutdown(), NrStatus::Success());
            ExpectStatus(server.Shutdown(), NrStatus::Success());
        }
    } // namespace
} // namespace psnr::runtime
