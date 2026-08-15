#include "pch.h"

#include <PrivateServer/NetworkRuntime/NrServer.h>
#include <PrivateServer/NetworkRuntime/NrServerSnapshot.h>
#include "NrServerTestUtils.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace psnr::runtime
{
    using psnr::core::NrErrorCode;
    using psnr::core::NrStatus;
    using tests::CreateServer;
    using tests::CreateServerConfig;
    using tests::ExpectStatus;

    namespace
    {
        TEST(NrServerSnapshotTests, PublicSnapshotIsCopyableOwningValue)
        {
            EXPECT_TRUE(std::is_nothrow_copy_constructible_v<NrServerSnapshot>);
            EXPECT_TRUE(std::is_nothrow_copy_assignable_v<NrServerSnapshot>);
            EXPECT_TRUE(std::is_nothrow_move_constructible_v<NrServerSnapshot>);
            EXPECT_TRUE(std::is_nothrow_move_assignable_v<NrServerSnapshot>);
            EXPECT_TRUE(std::is_trivially_copyable_v<NrServerDiagnosticsSnapshot>);
        }

        TEST(NrServerSnapshotTests, PublicEnumsUseFixedWidthStorage)
        {
            EXPECT_TRUE((std::is_same_v<std::underlying_type_t<NrServerLifecycleState>, std::uint8_t>));
            EXPECT_TRUE((std::is_same_v<std::underlying_type_t<NrPressureTransactionOutcome>, std::uint8_t>));
            EXPECT_TRUE((std::is_same_v<std::underlying_type_t<NrServerMemoryPoolRole>, std::uint8_t>));
            EXPECT_TRUE((std::is_same_v<std::underlying_type_t<NrPoolPressureOutcome>, std::uint8_t>));
        }

        TEST(NrServerSnapshotTests, DefaultSnapshotIsInvalidAndZeroInitialized)
        {
            const NrServerSnapshot snapshot;

            EXPECT_FALSE(snapshot.IsValid());
            EXPECT_EQ(snapshot.LifecycleState(), NrServerLifecycleState::Invalid);
            EXPECT_EQ(snapshot.RegisteredSessionCount(), 0u);
            EXPECT_EQ(snapshot.ClosingSessionCount(), 0u);
            EXPECT_EQ(snapshot.PendingRecvIoCount(), 0u);
            EXPECT_EQ(snapshot.PendingSendIoCount(), 0u);
            EXPECT_EQ(snapshot.PendingIoCount(), 0u);
            EXPECT_EQ(snapshot.SendMailboxDepth(), 0u);
            EXPECT_EQ(snapshot.SendMailboxHighWatermark(), 0u);
            EXPECT_EQ(snapshot.PendingSendQueueDepth(), 0u);
            EXPECT_EQ(snapshot.PendingSendQueueHighWatermark(), 0u);
            EXPECT_EQ(snapshot.ToWorldEventDepth(), 0u);
            EXPECT_EQ(snapshot.ToWorldEventHighWatermark(), 0u);
            EXPECT_EQ(snapshot.TotalPressureTransactions(), 0u);

            const NrServerDiagnosticsSnapshot diagnostics = snapshot.Diagnostics();
            EXPECT_FALSE(diagnostics.enabled);
            EXPECT_FALSE(diagnostics.sinkFailed);
            EXPECT_EQ(diagnostics.attempted, 0u);
            EXPECT_EQ(diagnostics.enqueued, 0u);
            EXPECT_EQ(diagnostics.droppedQueueFull, 0u);
            EXPECT_EQ(diagnostics.droppedSinkUnavailable, 0u);
            EXPECT_EQ(diagnostics.consumed, 0u);
            EXPECT_EQ(diagnostics.discardedAfterSinkFailure, 0u);

            EXPECT_EQ(snapshot.PressureTransactionCount(NrPressureTransactionOutcome::ToWorldPacketAdmissionRejected),
                      0u);
            EXPECT_EQ(
                snapshot.PoolAcquirePressureCount(NrServerMemoryPoolRole::Payload256, NrPoolPressureOutcome::Exhausted),
                0u);

            const NrServerMemoryPoolSnapshot pool = snapshot.MemoryPool(NrServerMemoryPoolRole::Payload256);
            EXPECT_EQ(pool.capacity, 0u);
            EXPECT_EQ(pool.inUse, 0u);
            EXPECT_EQ(pool.available, 0u);
            EXPECT_EQ(pool.highWatermark, 0u);
        }

        TEST(NrServerSnapshotTests, UnknownFixedDimensionsReadAsZero)
        {
            const NrServerSnapshot snapshot;

            EXPECT_EQ(snapshot.PressureTransactionCount(NrPressureTransactionOutcome::Count), 0u);
            EXPECT_EQ(
                snapshot.PoolAcquirePressureCount(NrServerMemoryPoolRole::Count, NrPoolPressureOutcome::Exhausted), 0u);
            EXPECT_EQ(
                snapshot.PoolAcquirePressureCount(NrServerMemoryPoolRole::Payload64, NrPoolPressureOutcome::Count), 0u);

            const NrServerMemoryPoolSnapshot pool = snapshot.MemoryPool(NrServerMemoryPoolRole::Count);
            EXPECT_EQ(pool.capacity, 0u);
            EXPECT_EQ(pool.inUse, 0u);
            EXPECT_EQ(pool.available, 0u);
            EXPECT_EQ(pool.highWatermark, 0u);
        }

        TEST(NrServerSnapshotTests, CreatedServerProjectsCentralMemoryPoolStats)
        {
            const NrServerConfig config = CreateServerConfig();
            NrServer server;
            ASSERT_TRUE(NrServer::Create(config, &server).Succeeded());

            NrServerSnapshot snapshot;
            ExpectStatus(server.CaptureSnapshot(&snapshot), NrStatus::Success());

            EXPECT_TRUE(snapshot.IsValid());
            EXPECT_EQ(snapshot.LifecycleState(), NrServerLifecycleState::Created);
            EXPECT_EQ(snapshot.RegisteredSessionCount(), 0u);
            EXPECT_EQ(snapshot.ClosingSessionCount(), 0u);
            EXPECT_EQ(snapshot.PendingRecvIoCount(), 0u);
            EXPECT_EQ(snapshot.PendingSendIoCount(), 0u);
            EXPECT_EQ(snapshot.PendingIoCount(), snapshot.PendingRecvIoCount() + snapshot.PendingSendIoCount());
            EXPECT_EQ(snapshot.SendMailboxDepth(), 0u);
            EXPECT_EQ(snapshot.SendMailboxHighWatermark(), 0u);
            EXPECT_EQ(snapshot.PendingSendQueueDepth(), 0u);
            EXPECT_EQ(snapshot.PendingSendQueueHighWatermark(), 0u);
            EXPECT_EQ(snapshot.ToWorldEventDepth(), 0u);
            EXPECT_EQ(snapshot.ToWorldEventHighWatermark(), 0u);

            const NrServerDiagnosticsSnapshot diagnostics = snapshot.Diagnostics();
            EXPECT_FALSE(diagnostics.enabled);
            EXPECT_FALSE(diagnostics.sinkFailed);
            EXPECT_EQ(diagnostics.attempted, 0u);
            EXPECT_EQ(diagnostics.enqueued, 0u);
            EXPECT_EQ(diagnostics.droppedQueueFull, 0u);
            EXPECT_EQ(diagnostics.droppedSinkUnavailable, 0u);
            EXPECT_EQ(diagnostics.consumed, 0u);
            EXPECT_EQ(diagnostics.discardedAfterSinkFailure, 0u);

            const NrServerMemoryPoolSnapshot recvBuffer = snapshot.MemoryPool(NrServerMemoryPoolRole::RecvBuffer);
            EXPECT_EQ(recvBuffer.capacity, config.maxSessionCount);

            const NrServerMemoryPoolSnapshot toWorld =
                snapshot.MemoryPool(NrServerMemoryPoolRole::ToWorldEventQueueStorage);
            EXPECT_EQ(toWorld.capacity, 1u);
            EXPECT_EQ(toWorld.inUse, 1u);

            const NrServerMemoryPoolSnapshot actorReady =
                snapshot.MemoryPool(NrServerMemoryPoolRole::ActorReadyQueueStorage);
            EXPECT_EQ(actorReady.capacity, 2u);
            EXPECT_EQ(actorReady.inUse, 2u);

            for (std::size_t outcomeIndex = 0; outcomeIndex < NrPressureTransactionOutcomeCount; ++outcomeIndex)
            {
                const NrPressureTransactionOutcome outcome = static_cast<NrPressureTransactionOutcome>(outcomeIndex);
                EXPECT_EQ(snapshot.PressureTransactionCount(outcome), 0u);
            }

            for (std::size_t roleIndex = 0; roleIndex < NrServerMemoryPoolRoleCount; ++roleIndex)
            {
                const NrServerMemoryPoolRole role = static_cast<NrServerMemoryPoolRole>(roleIndex);
                EXPECT_EQ(snapshot.PoolAcquirePressureCount(role, NrPoolPressureOutcome::Exhausted), 0u);
            }
            EXPECT_EQ(snapshot.TotalPressureTransactions(), 0u);
        }

        TEST(NrServerSnapshotTests, InvalidServerCaptureDoesNotChangeOutput)
        {
            NrServer source = CreateServer();
            NrServerSnapshot snapshot;
            ASSERT_TRUE(source.CaptureSnapshot(&snapshot).Succeeded());
            const NrServerMemoryPoolSnapshot before =
                snapshot.MemoryPool(NrServerMemoryPoolRole::ActorReadyQueueStorage);

            NrServer target(std::move(source));
            static_cast<void>(target);

            ExpectStatus(source.CaptureSnapshot(&snapshot), NrStatus::Failure(NrErrorCode::InvalidState));
            EXPECT_TRUE(snapshot.IsValid());
            EXPECT_EQ(snapshot.MemoryPool(NrServerMemoryPoolRole::ActorReadyQueueStorage).capacity, before.capacity);
        }

        TEST(NrServerSnapshotTests, CaptureSnapshotRejectsNullOutput)
        {
            NrServer server = CreateServer();

            ExpectStatus(server.CaptureSnapshot(nullptr), NrStatus::Failure(NrErrorCode::InvalidArgument));
        }

        TEST(NrServerSnapshotTests, CaptureSnapshotProjectsConfiguredPayloadPoolCapacities)
        {
            NrServerConfig config = CreateServerConfig();
            config.payloadPools.payload64BlockCount = 2048;
            config.payloadPools.payload256BlockCount = 512;
            config.payloadPools.payload1024BlockCount = 256;
            config.payloadPools.payload8192BlockCount = 64;
            config.payloadPools.payloadRefControlBlockCount = 4096;

            NrServer server;
            ASSERT_TRUE(NrServer::Create(config, &server).Succeeded());

            NrServerSnapshot snapshot;
            ExpectStatus(server.CaptureSnapshot(&snapshot), NrStatus::Success());
            EXPECT_EQ(snapshot.MemoryPool(NrServerMemoryPoolRole::Payload64).capacity, 2048u);
            EXPECT_EQ(snapshot.MemoryPool(NrServerMemoryPoolRole::Payload256).capacity, 512u);
            EXPECT_EQ(snapshot.MemoryPool(NrServerMemoryPoolRole::Payload1024).capacity, 256u);
            EXPECT_EQ(snapshot.MemoryPool(NrServerMemoryPoolRole::Payload8192).capacity, 64u);
            EXPECT_EQ(snapshot.MemoryPool(NrServerMemoryPoolRole::PayloadRefControl).capacity, 4096u);
        }

        TEST(NrServerSnapshotTests, DebugServerProjectsDiagnosticsHealthBeforeAndAfterShutdown)
        {
            NrServerConfig config = CreateServerConfig();
            config.diagnostics.mode = NrDiagnosticsMode::Debug;

            NrServer server;
            ASSERT_TRUE(NrServer::Create(config, &server).Succeeded());

            NrServerSnapshot createdSnapshot;
            ExpectStatus(server.CaptureSnapshot(&createdSnapshot), NrStatus::Success());
            const NrServerDiagnosticsSnapshot createdDiagnostics = createdSnapshot.Diagnostics();
            EXPECT_TRUE(createdDiagnostics.enabled);
            EXPECT_FALSE(createdDiagnostics.sinkFailed);
            EXPECT_EQ(createdDiagnostics.attempted, 0u);

            ExpectStatus(server.Start(), NrStatus::Success());
            ExpectStatus(server.RequestStop(), NrStatus::Success());
            ExpectStatus(server.Shutdown(), NrStatus::Success());

            NrServerSnapshot shutdownSnapshot;
            ExpectStatus(server.CaptureSnapshot(&shutdownSnapshot), NrStatus::Success());
            const NrServerDiagnosticsSnapshot shutdownDiagnostics = shutdownSnapshot.Diagnostics();
            EXPECT_TRUE(shutdownDiagnostics.enabled);
            EXPECT_FALSE(shutdownDiagnostics.sinkFailed);
            EXPECT_EQ(shutdownDiagnostics.attempted, 0u);
            EXPECT_EQ(shutdownDiagnostics.enqueued, 0u);
            EXPECT_EQ(shutdownDiagnostics.consumed, 0u);
        }

        TEST(NrServerSnapshotTests, BenchmarkOpenFailureIsProjectedWithoutFailingServerStart)
        {
            const auto uniqueId = std::chrono::steady_clock::now().time_since_epoch().count();
            const std::filesystem::path outputPath =
                std::filesystem::temp_directory_path() /
                ("psnr-missing-diagnostics-directory-" + std::to_string(uniqueId)) / "diagnostics.jsonl";
            const std::u8string encodedPath = outputPath.u8string();
            const std::string outputPathUtf8(reinterpret_cast<const char*>(encodedPath.data()), encodedPath.size());

            NrServerConfig config = CreateServerConfig();
            config.diagnostics = NrDiagnosticsConfig{
                NrDiagnosticsMode::Benchmark,
                NrUtf8View{outputPathUtf8.data(), static_cast<std::uint32_t>(outputPathUtf8.size())},
            };

            NrServer server;
            ASSERT_TRUE(NrServer::Create(config, &server).Succeeded());
            ExpectStatus(server.Start(), NrStatus::Success());

            NrServerSnapshot snapshot;
            ExpectStatus(server.CaptureSnapshot(&snapshot), NrStatus::Success());
            const NrServerDiagnosticsSnapshot diagnostics = snapshot.Diagnostics();
            EXPECT_TRUE(diagnostics.enabled);
            EXPECT_TRUE(diagnostics.sinkFailed);
            EXPECT_EQ(diagnostics.attempted, 0u);

            ExpectStatus(server.RequestStop(), NrStatus::Success());
            ExpectStatus(server.Shutdown(), NrStatus::Success());
        }

        TEST(NrServerSnapshotTests, CapturedSnapshotRemainsOwningValueAfterLaterCapture)
        {
            NrServer server = CreateServer();
            NrServerSnapshot latestSnapshot;
            ExpectStatus(server.CaptureSnapshot(&latestSnapshot), NrStatus::Success());
            ASSERT_EQ(latestSnapshot.LifecycleState(), NrServerLifecycleState::Created);

            const NrServerSnapshot preservedSnapshot = latestSnapshot;
            const NrServerMemoryPoolSnapshot preservedRecvBuffer =
                preservedSnapshot.MemoryPool(NrServerMemoryPoolRole::RecvBuffer);

            ExpectStatus(server.Shutdown(), NrStatus::Success());
            ExpectStatus(server.CaptureSnapshot(&latestSnapshot), NrStatus::Success());

            EXPECT_EQ(latestSnapshot.LifecycleState(), NrServerLifecycleState::Shutdown);
            EXPECT_EQ(preservedSnapshot.LifecycleState(), NrServerLifecycleState::Created);
            EXPECT_EQ(preservedSnapshot.MemoryPool(NrServerMemoryPoolRole::RecvBuffer).capacity,
                      preservedRecvBuffer.capacity);
        }

        TEST(NrServerSnapshotTests, ConcurrentReadersCaptureBoundedRunningSnapshots)
        {
            constexpr std::size_t ReaderCount = 4;
            constexpr std::size_t CaptureCountPerReader = 64;

            const NrServerConfig config = CreateServerConfig();
            NrServer server;
            ASSERT_TRUE(NrServer::Create(config, &server).Succeeded());
            ExpectStatus(server.Start(), NrStatus::Success());

            std::atomic_bool allSnapshotsValid{true};
            {
                std::vector<std::jthread> readers;
                readers.reserve(ReaderCount);
                for (std::size_t readerIndex = 0; readerIndex < ReaderCount; ++readerIndex)
                {
                    readers.emplace_back(
                        [&server, &config, &allSnapshotsValid]() noexcept
                        {
                            for (std::size_t captureIndex = 0; captureIndex < CaptureCountPerReader; ++captureIndex)
                            {
                                NrServerSnapshot snapshot;
                                if (server.CaptureSnapshot(&snapshot).Failed() || !snapshot.IsValid() ||
                                    snapshot.LifecycleState() != NrServerLifecycleState::Running ||
                                    snapshot.RegisteredSessionCount() > config.maxSessionCount ||
                                    snapshot.ClosingSessionCount() > snapshot.RegisteredSessionCount() ||
                                    snapshot.PendingRecvIoCount() > config.maxSessionCount ||
                                    snapshot.PendingSendIoCount() > config.maxSessionCount ||
                                    snapshot.SendMailboxDepth() > snapshot.SendMailboxHighWatermark() ||
                                    snapshot.PendingSendQueueDepth() > snapshot.PendingSendQueueHighWatermark() ||
                                    snapshot.ToWorldEventDepth() > config.toWorldEventCapacity ||
                                    snapshot.ToWorldEventHighWatermark() > config.toWorldEventCapacity ||
                                    snapshot.ToWorldEventDepth() > snapshot.ToWorldEventHighWatermark())
                                {
                                    allSnapshotsValid.store(false, std::memory_order_relaxed);
                                    return;
                                }

                                for (std::size_t roleIndex = 0; roleIndex < NrServerMemoryPoolRoleCount; ++roleIndex)
                                {
                                    const NrServerMemoryPoolRole role = static_cast<NrServerMemoryPoolRole>(roleIndex);
                                    const NrServerMemoryPoolSnapshot pool = snapshot.MemoryPool(role);
                                    if (pool.inUse > pool.capacity || pool.available > pool.capacity ||
                                        pool.highWatermark > pool.capacity ||
                                        pool.inUse + pool.available != pool.capacity)
                                    {
                                        allSnapshotsValid.store(false, std::memory_order_relaxed);
                                        return;
                                    }
                                }
                            }
                        });
                }
            }

            EXPECT_TRUE(allSnapshotsValid.load(std::memory_order_relaxed));
            ExpectStatus(server.RequestStop(), NrStatus::Success());
            ExpectStatus(server.Shutdown(), NrStatus::Success());
        }
    } // namespace
} // namespace psnr::runtime
