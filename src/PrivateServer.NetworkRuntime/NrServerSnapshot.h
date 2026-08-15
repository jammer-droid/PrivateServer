#pragma once

#include "Export.h"
#include "NrServerPressureTypes.h"

#include <cstddef>
#include <cstdint>

namespace psnr::runtime
{
    namespace internal
    {
        class NrServerSnapshotAccess;
    }

    enum class NrServerLifecycleState : std::uint8_t
    {
        Invalid,       // 유효한 Server에서 만들어지지 않은 Snapshot
        Created,       // Server Graph 및 Configuration 완료. Start 시작 전
        Running,       // IOCP, Listener, Actor Worker 등이 실행 중
        StopRequested, // 신규 요청을 막고 종료 절차 진행 중
        Shutdown,      // runtime component 종료 완료
    };

    struct NrServerMemoryPoolSnapshot final
    {
        std::uint64_t capacity = 0;
        std::uint64_t inUse = 0;
        std::uint64_t available = 0;
        std::uint64_t highWatermark = 0;
    };

    struct NrServerDiagnosticsSnapshot final
    {
        bool enabled = false;
        bool sinkFailed = false;
        std::uint64_t attempted = 0;
        std::uint64_t enqueued = 0;
        std::uint64_t droppedQueueFull = 0;
        std::uint64_t droppedSinkUnavailable = 0;
        std::uint64_t consumed = 0;
        std::uint64_t discardedAfterSinkFailure = 0;
    };

    class NrServerSnapshot final
    {
    public:
        PSNR_API NrServerSnapshot() noexcept;

        NrServerSnapshot(const NrServerSnapshot&) noexcept = default;
        NrServerSnapshot& operator=(const NrServerSnapshot&) noexcept = default;

        NrServerSnapshot(NrServerSnapshot&&) noexcept = default;
        NrServerSnapshot& operator=(NrServerSnapshot&&) noexcept = default;

        PSNR_API ~NrServerSnapshot() noexcept;

        [[nodiscard]] PSNR_API bool IsValid() const noexcept;
        [[nodiscard]] PSNR_API NrServerLifecycleState LifecycleState() const noexcept;

        [[nodiscard]] PSNR_API std::uint64_t RegisteredSessionCount() const noexcept;
        [[nodiscard]] PSNR_API std::uint64_t ClosingSessionCount() const noexcept;
        [[nodiscard]] PSNR_API std::uint64_t PendingRecvIoCount() const noexcept;
        [[nodiscard]] PSNR_API std::uint64_t PendingSendIoCount() const noexcept;
        [[nodiscard]] PSNR_API std::uint64_t PendingIoCount() const noexcept;

        [[nodiscard]] PSNR_API std::uint64_t SendMailboxDepth() const noexcept;
        [[nodiscard]] PSNR_API std::uint64_t SendMailboxHighWatermark() const noexcept;
        [[nodiscard]] PSNR_API std::uint64_t PendingSendQueueDepth() const noexcept;
        [[nodiscard]] PSNR_API std::uint64_t PendingSendQueueHighWatermark() const noexcept;

        [[nodiscard]] PSNR_API std::uint64_t ToWorldEventDepth() const noexcept;
        [[nodiscard]] PSNR_API std::uint64_t ToWorldEventHighWatermark() const noexcept;

        [[nodiscard]] PSNR_API std::uint64_t PressureTransactionCount(
            NrPressureTransactionOutcome outcome) const noexcept;
        [[nodiscard]] PSNR_API std::uint64_t PoolAcquirePressureCount(NrServerMemoryPoolRole role,
                                                                      NrPoolPressureOutcome outcome) const noexcept;
        [[nodiscard]] PSNR_API std::uint64_t TotalPressureTransactions() const noexcept;

        [[nodiscard]] PSNR_API NrServerMemoryPoolSnapshot MemoryPool(NrServerMemoryPoolRole role) const noexcept;
        [[nodiscard]] PSNR_API NrServerDiagnosticsSnapshot Diagnostics() const noexcept;

    private:
        friend class internal::NrServerSnapshotAccess;

        bool isValid_ = false;
        NrServerLifecycleState lifecycleState_ = NrServerLifecycleState::Invalid; // Server 상태

        // Runtime 현재 상태
        std::uint64_t registeredSessionCount_ = 0; // SessionActorRegistry에 등록된 session 의 수
        std::uint64_t closingSessionCount_ = 0;    // Close 절차를 진행 중인 session 의 수
        std::uint64_t pendingRecvIoCount_ = 0;     // post 후 completion을 아직 consume하지 않은 recv IO 작업 수
        std::uint64_t pendingSendIoCount_ = 0;     // post 후 completion을 아직 consume하지 않은 send IO 작업 수

        // World/producer가 제출한 send가 Runtime 내부의 어느 단계에 쌓였는지 구분한다.
        std::uint64_t sendMailboxDepth_ = 0;
        std::uint64_t sendMailboxHighWatermark_ = 0;
        std::uint64_t pendingSendQueueDepth_ = 0;
        std::uint64_t pendingSendQueueHighWatermark_ = 0;

        std::uint64_t toWorldEventDepth_ = 0;         // To-World Event queue에 쌓여 있는 이벤트의 수
        std::uint64_t toWorldEventHighWatermark_ = 0; // To-World EVent queue에 쌓인 이벤트의 수가 가장 높았을 때

        // Pressure 누적 통계
        std::uint64_t pressureTransactionCounts_[NrPressureTransactionOutcomeCount]{};
        std::uint64_t poolAcquirePressureCounts_[NrServerMemoryPoolRoleCount][NrPoolPressureOutcomeCount]{};
        std::uint64_t totalPressureTransactions_ = 0; // 모든 pressure outcome의 누적 합계

        // MemoryPool 상태
        NrServerMemoryPoolSnapshot memoryPools_[NrServerMemoryPoolRoleCount]{};

        NrServerDiagnosticsSnapshot diagnostics_{};
    };
} // namespace psnr::runtime
