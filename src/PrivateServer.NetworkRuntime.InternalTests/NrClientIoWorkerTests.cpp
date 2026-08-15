#include "pch.h"

#include "NrClientControlCompletion.h"
#include "NrClientIoWorker.h"
#include "NrErrorCode.h"
#include "NrIocpPort.h"

#include <chrono>
#include <cstddef>
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace psnr::runtime::internal
{
    namespace
    {
        class NrRecordingClientIoCompletionTarget final : public INrClientIoCompletionTarget
        {
        public:
            [[nodiscard]] std::uint64_t CurrentAttemptGeneration() const noexcept override
            {
                return 1;
            }

            [[nodiscard]] psnr::core::NrStatus HandleClientControlCompletion(
                const NrClientControlCompletionKind kind) noexcept override
            {
                if (workerToStop != nullptr)
                {
                    selfStopStatus = workerToStop->Stop();
                }

                {
                    const std::lock_guard<std::mutex> lock(controlMutex);
                    recordedControlKind = kind;
                    ++controlCount;
                }
                controlCondition.notify_all();
                return psnr::core::NrStatus::Success();
            }

            [[nodiscard]] psnr::core::NrStatus HandleConnectCompletion(
                NrClientConnectIoContext&, const NrIocpCompletionPacket&) noexcept override
            {
                return psnr::core::NrStatus::Success();
            }

            [[nodiscard]] psnr::core::NrStatus HandleRecvCompletion(
                NrRecvIoContext&, const NrIocpCompletionPacket&) noexcept override
            {
                return psnr::core::NrStatus::Success();
            }

            [[nodiscard]] psnr::core::NrStatus HandleSendCompletion(
                NrSendIoContext&, const NrIocpCompletionPacket&) noexcept override
            {
                return psnr::core::NrStatus::Success();
            }

            [[nodiscard]] psnr::core::NrStatus HandleStaleIoCompletion(
                NrIoOperationType, std::uint64_t, const NrIocpCompletionPacket&) noexcept override
            {
                return psnr::core::NrStatus::Success();
            }

            [[nodiscard]] bool WaitForControl() noexcept
            {
                std::unique_lock<std::mutex> lock(controlMutex);
                return controlCondition.wait_for(lock, std::chrono::seconds(1),
                                                 [this]() noexcept { return controlCount > 0; });
            }

            NrClientControlCompletionKind recordedControlKind = NrClientControlCompletionKind::None;
            std::size_t controlCount = 0;
            NrClientIoWorker* workerToStop = nullptr;
            psnr::core::NrStatus selfStopStatus;
            std::mutex controlMutex;
            std::condition_variable controlCondition;
        };

        TEST(NrClientIoWorkerTests, PumpsTargetControlCompletionUntilWorkerStop)
        {
            NrIocpPort iocpPort;
            ASSERT_TRUE(iocpPort.Create().Succeeded());
            NrRecordingClientIoCompletionTarget target;
            NrClientIoWorker worker(iocpPort, target);

            ASSERT_TRUE(worker.Start().Succeeded());
            EXPECT_TRUE(worker.IsRunning());
            ASSERT_TRUE(
                PostClientControlCompletion(iocpPort, NrClientControlCompletionKind::EventSpaceAvailable).Succeeded());
            ASSERT_TRUE(target.WaitForControl());

            ASSERT_TRUE(worker.Stop().Succeeded());
            EXPECT_FALSE(worker.IsRunning());
            EXPECT_EQ(target.recordedControlKind, NrClientControlCompletionKind::EventSpaceAvailable);
            EXPECT_EQ(target.controlCount, 1u);
        }

        TEST(NrClientIoWorkerTests, RejectsDuplicateStartAndAllowsRepeatedStop)
        {
            NrIocpPort iocpPort;
            ASSERT_TRUE(iocpPort.Create().Succeeded());
            NrRecordingClientIoCompletionTarget target;
            NrClientIoWorker worker(iocpPort, target);

            ASSERT_TRUE(worker.Start().Succeeded());
            EXPECT_EQ(worker.Start().ErrorCode(), psnr::core::NrErrorCode::InvalidState);
            ASSERT_TRUE(worker.Stop().Succeeded());
            EXPECT_TRUE(worker.Stop().Succeeded());
        }

        TEST(NrClientIoWorkerTests, JoinWaitsForPostedStopAndReturnsWorkerExitStatus)
        {
            NrIocpPort iocpPort;
            ASSERT_TRUE(iocpPort.Create().Succeeded());
            NrRecordingClientIoCompletionTarget target;
            NrClientIoWorker worker(iocpPort, target);

            ASSERT_TRUE(worker.Start().Succeeded());
            ASSERT_TRUE(PostClientControlCompletion(iocpPort, NrClientControlCompletionKind::Stop).Succeeded());
            EXPECT_TRUE(worker.Join().Succeeded());
            EXPECT_FALSE(worker.IsRunning());
            EXPECT_TRUE(worker.Join().Succeeded());
        }

        TEST(NrClientIoWorkerTests, WorkerThreadStopAvoidsSelfJoinAndOwnerJoinsAfterExit)
        {
            NrIocpPort iocpPort;
            ASSERT_TRUE(iocpPort.Create().Succeeded());
            NrRecordingClientIoCompletionTarget target;
            NrClientIoWorker worker(iocpPort, target);
            target.workerToStop = &worker;

            ASSERT_TRUE(worker.Start().Succeeded());
            ASSERT_TRUE(
                PostClientControlCompletion(iocpPort, NrClientControlCompletionKind::EventSpaceAvailable).Succeeded());
            ASSERT_TRUE(target.WaitForControl());

            EXPECT_TRUE(target.selfStopStatus.Succeeded());
            EXPECT_TRUE(worker.Join().Succeeded());
            EXPECT_FALSE(worker.IsRunning());
        }

        TEST(NrClientIoWorkerTests, JoinReturnsTerminalPumpFailure)
        {
            NrIocpPort iocpPort;
            ASSERT_TRUE(iocpPort.Create().Succeeded());
            NrRecordingClientIoCompletionTarget target;
            NrClientIoWorker worker(iocpPort, target);

            ASSERT_TRUE(worker.Start().Succeeded());
            ASSERT_TRUE(iocpPort.PostControlCompletion(0).Succeeded());

            const psnr::core::NrStatus joinStatus = worker.Join();
            EXPECT_EQ(joinStatus.ErrorCode(), psnr::core::NrErrorCode::InvalidArgument);
            EXPECT_FALSE(worker.IsRunning());
        }
    } // namespace
} // namespace psnr::runtime::internal
