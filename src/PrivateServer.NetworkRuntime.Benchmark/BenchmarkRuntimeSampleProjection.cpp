#include "BenchmarkRuntimeSampleProjection.h"

#include <cstddef>
#include <string>
#include <utility>

namespace psnr::benchmark
{
    namespace
    {
        [[nodiscard]] BenchmarkRuntimeSampleProjectionResult ProjectionFailure(std::string error)
        {
            BenchmarkRuntimeSampleProjectionResult result;
            result.error = std::move(error);
            return result;
        }

        [[nodiscard]] bool IsSupportedLifecycleState(const psnr::runtime::NrServerLifecycleState state) noexcept
        {
            switch (state)
            {
            case psnr::runtime::NrServerLifecycleState::Created:
            case psnr::runtime::NrServerLifecycleState::Running:
            case psnr::runtime::NrServerLifecycleState::StopRequested:
            case psnr::runtime::NrServerLifecycleState::Shutdown:
                return true;
            case psnr::runtime::NrServerLifecycleState::Invalid:
                return false;
            }

            return false;
        }
    } // namespace

    BenchmarkRuntimeSampleProjectionResult BenchmarkRuntimeSampleProjection::Project(
        const psnr::runtime::NrServerSnapshot& snapshot)
    {
        if (!snapshot.IsValid())
        {
            return ProjectionFailure("NrServerSnapshot must be valid");
        }

        const psnr::runtime::NrServerLifecycleState lifecycleState = snapshot.LifecycleState();
        if (!IsSupportedLifecycleState(lifecycleState))
        {
            return ProjectionFailure("NrServerSnapshot lifecycle state is unsupported by IPC schema v1");
        }

        BenchmarkRuntimeSampleProjectionResult result;
        BenchmarkRuntimeSampleV1& sample = result.sample;
        sample.lifecycleState = lifecycleState;
        sample.registeredSessionCount = snapshot.RegisteredSessionCount();
        sample.closingSessionCount = snapshot.ClosingSessionCount();
        sample.pendingRecvIoCount = snapshot.PendingRecvIoCount();
        sample.pendingSendIoCount = snapshot.PendingSendIoCount();
        sample.toWorldEventDepth = snapshot.ToWorldEventDepth();
        sample.toWorldEventHighWatermark = snapshot.ToWorldEventHighWatermark();

        for (std::size_t index = 0; index < psnr::runtime::NrPressureTransactionOutcomeCount; ++index)
        {
            const psnr::runtime::NrPressureTransactionOutcome outcome =
                static_cast<psnr::runtime::NrPressureTransactionOutcome>(index);
            sample.pressureTransactionCounts[index] = snapshot.PressureTransactionCount(outcome);
        }

        for (std::size_t index = 0; index < psnr::runtime::NrServerMemoryPoolRoleCount; ++index)
        {
            const psnr::runtime::NrServerMemoryPoolRole role =
                static_cast<psnr::runtime::NrServerMemoryPoolRole>(index);
            sample.poolExhaustionCounts[index] =
                snapshot.PoolAcquirePressureCount(role, psnr::runtime::NrPoolPressureOutcome::Exhausted);
            sample.memoryPools[index] = snapshot.MemoryPool(role);
        }

        sample.totalPressureTransactions = snapshot.TotalPressureTransactions();
        sample.diagnostics = snapshot.Diagnostics();
        return result;
    }
} // namespace psnr::benchmark
