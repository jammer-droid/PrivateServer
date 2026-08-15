#pragma once

#include <atomic>
#include <cstdint>

namespace psnr::core
{
    enum class NrActorScheduleView : std::uint8_t // 스케줄러가 확인할 Gate Phase에 대한 view
    {
        Idle,
        Admitting,
        Scheduled,
        Draining,
        DrainingWithPending,
        DrainCompleting,
    };

    enum class NrActorAdmissionDecision : std::uint8_t
    {
        Rejected,           // admission 시작하지 못함
        RequiresNewPermit,  // Idle actor이므로 새 runnable permit이 필요함
        UsesExistingPermit, // actor가 이미 보유한 runnable permit을 사용함
    };

    class NrActorAdmissionTicket final
    {
    public:
        NrActorAdmissionTicket(const NrActorAdmissionTicket&) = delete;
        NrActorAdmissionTicket& operator=(const NrActorAdmissionTicket&) = delete;

        NrActorAdmissionTicket(NrActorAdmissionTicket&& other) noexcept
            : decision_(other.decision_)
        {
            other.decision_ = NrActorAdmissionDecision::Rejected;
        }

        NrActorAdmissionTicket& operator=(NrActorAdmissionTicket&&) = delete;

        ~NrActorAdmissionTicket() noexcept = default;

        [[nodiscard]] static NrActorAdmissionTicket Rejected() noexcept
        {
            return NrActorAdmissionTicket();
        }

        [[nodiscard]] bool Accepted() const noexcept
        {
            return decision_ != NrActorAdmissionDecision::Rejected;
        }

        [[nodiscard]] bool NeedsNewPermit() const noexcept
        {
            return decision_ == NrActorAdmissionDecision::RequiresNewPermit;
        }

    private:
        NrActorAdmissionTicket() noexcept = default;

        explicit NrActorAdmissionTicket(NrActorAdmissionDecision decision) noexcept
            : decision_(decision)
        {
        }

        NrActorAdmissionDecision decision_ = NrActorAdmissionDecision::Rejected;

        friend class NrActorScheduleGate;
    };

    enum class NrActorAdmissionResolution : std::uint8_t
    {
        CapacityRejected, // 신규 runnable permit capacity 부족
        MailboxRejected,  // mailbox enqueue fail
        MailboxCommitted, // success
    };

    enum class NrActorScheduleDirective : std::uint8_t
    {
        NoAction,             // 스케줄러 추가 작업 없음
        FinalizationDeferred, // worker drain 은 끝났지만, admission 진행 중인 producer 존재(상태 확정을 producer에서 진행)
        EnqueueReadyToken,    // actor 실행을 위해 ready queue에 추가 요청
        ReleasePermit,        // actor가 더 이상 runnable 하지 않으므로 permit 반환
    };

    class NrActorScheduleGate
    {
    public:
        NrActorScheduleGate() = default;

        NrActorScheduleGate(const NrActorScheduleGate&) = delete;
        NrActorScheduleGate& operator=(const NrActorScheduleGate&) = delete;

        NrActorScheduleGate(NrActorScheduleGate&&) = delete;
        NrActorScheduleGate& operator=(NrActorScheduleGate&&) = delete;

        ~NrActorScheduleGate() noexcept = default;

        [[nodiscard]] NrActorScheduleView View() const noexcept;

        [[nodiscard]] NrActorAdmissionTicket TryBeginAdmission() noexcept;
        [[nodiscard]] NrActorScheduleDirective CompleteAdmission(NrActorAdmissionTicket&& ticket,
                                                                 NrActorAdmissionResolution resolution) noexcept;
        [[nodiscard]] NrActorScheduleDirective CompleteDrain(bool shouldReschedule) noexcept;

        [[nodiscard]] bool TryBeginDrain() noexcept;

    private:
        using NrActorScheduleStateWord = std::uint64_t;

        enum class NrPackedPhase : std::uint8_t // ScheduleGate의 의미상 상태
        {
            Idle,            // 초기
            Admitting,       // Idle Actor를 runnable 집합에 등록하는 admission transaction 진행 중
            Scheduled,       // Admitting에서 mailbox에 mail이 들어왔고, 스케줄링된 상태
            Draining,        // Actor worker Drain 중
            DrainCompleting, // Actor worker Drain은 완료, 동시에 진행 중인 producer admission 이 있어서 다음 상태 확정을 보류
        };

        /*
         * scheduleState_ 비트 배치
         *
         *  63                              40 39                           8 7 6 5 4 3 2 1 0
         * +----------------------------------+-----------------------------+-----+-+-+-----+
         * |              예약                 | producerInFlight (32비트)   | 예약 |R|P|phase|
         * +----------------------------------+-----------------------------+-----+-+-+-----+
         *
         * P: drain 중 mailbox commit이 발생했음을 나타내는 pending 비트
         * R: drain budget 또는 actor 결과가 재스케줄을 요구한 drainNeedsReschedule 비트
         * producerInFlight: 해당 actor 대상으로 admission transaction은 시작, mailbox commit 성공/실패는 아직 확정되지 않은 producer 수
         *                      -> 이 값이 0보다 크면 worker 는 Idle/permit 반환을 확정하면 안 됨(producer에서 mail 을 추가할 수 있음)
         *
         * phase와 producerInFlight를 별도 atomic으로 두면 worker의 Idle 전환과 producer 진입
         * 사이에 check-then-act 경쟁이 생긴다. 하나의 64비트 CAS로 묶어 둘 중 한 전이만
         * 먼저 성공하게 만든다.
         *
         * 주요 상태 전이
         *
         * [Idle] --새 admission 선점--> [Admitting] --commit--> [Scheduled]
         *    ^                              |                       |
         *    +------ reject/abort ----------+                       v
         *                                                       [Draining]
         *                                                           |
         *                                                      CompleteDrain
         *                                                           |
         *                         +---------------------------------+---------------------------------+
         *                         |                                                                   |
         *             producerInFlight == 0                                              producerInFlight > 0
         *                         |                                                                   |
         *                         v                                                                   v
         *              [Scheduled 또는 Idle]                                                   [DrainCompleting]
         *                                                                                             |
         *                                                                        마지막 producer admission transaction 완료
         *                                                                                             |
         *                                                                                             v
         *                                                                                   [Scheduled 또는 Idle]
         */
        static constexpr NrActorScheduleStateWord PhaseMask = 0x7ULL;                  // Phase (0b0000'0111)
        static constexpr NrActorScheduleStateWord PendingBit = 1ULL << 3;              // bit P
        static constexpr NrActorScheduleStateWord DrainNeedsRescheduleBit = 1ULL << 4; // bit R
        static constexpr std::uint32_t ProducerInFlightShift = 8;
        static constexpr NrActorScheduleStateWord ProducerInFlightMask = 0xFFFFFFFFULL << ProducerInFlightShift;
        static constexpr std::uint32_t MaxProducerInFlight = 0xFFFFFFFFU;

        // bit masking operations
        [[nodiscard]] static NrPackedPhase PackedPhase(NrActorScheduleStateWord state) noexcept;
        [[nodiscard]] static std::uint32_t ProducerInFlight(NrActorScheduleStateWord state) noexcept;
        [[nodiscard]] static bool HasBit(NrActorScheduleStateWord state, NrActorScheduleStateWord bit) noexcept;
        [[nodiscard]] static NrActorScheduleStateWord WithPhase(NrActorScheduleStateWord state,
                                                                NrPackedPhase phase) noexcept;
        [[nodiscard]] static NrActorScheduleStateWord WithProducerInFlight(NrActorScheduleStateWord state,
                                                                           std::uint32_t count) noexcept;
        [[nodiscard]] static NrActorScheduleStateWord ClearDrainFlags(NrActorScheduleStateWord state) noexcept;
        [[nodiscard]] static NrActorScheduleView DecodeView(NrActorScheduleStateWord state) noexcept;

        static_assert(std::atomic<NrActorScheduleStateWord>::is_always_lock_free,
                      "NrActorScheduleGate requires a lock-free 64-bit atomic schedule word.");

        alignas(sizeof(NrActorScheduleStateWord)) std::atomic<NrActorScheduleStateWord> scheduleState_{0};
    };
} // namespace psnr::core
