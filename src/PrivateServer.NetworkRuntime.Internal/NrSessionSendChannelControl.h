#pragma once

#include "NrActorScheduleHandle.h"
#include "NrPayloadRef.h"
#include "NrServerSubmissionGate.h"
#include "NrSessionKey.h"

#include <atomic>
#include <cstddef>

namespace psnr::runtime
{
    struct NrSessionSendChannelControl final
    {
    public:
        NrSessionSendChannelControl(const NrSessionSendChannelControl&) = delete;
        NrSessionSendChannelControl& operator=(const NrSessionSendChannelControl&) = delete;

        [[nodiscard]] static psnr::core::NrResult<NrSessionSendChannelControl*> Create(
            psnr::core::NrSessionKey sessionKey, const NrActorScheduleHandle& scheduleHandle,
            internal::NrSubmissionAdmissionHandle submissionAdmission) noexcept;

        void AddRef() noexcept;
        void ReleaseRef() noexcept;

        [[nodiscard]] bool IsOpen() const noexcept;
        void MarkClosed() noexcept;

        [[nodiscard]] psnr::core::NrStatus EnqueueSendRequested(
            psnr::core::NrPayloadRef payload, const internal::NrSubmissionPermit& permit) const noexcept;

    private:
        enum class NrState
        {
            Open,
            Closed,
        };

        NrSessionSendChannelControl(psnr::core::NrSessionKey ownerSessionKey,
                                    NrActorScheduleHandle ownerScheduleHandle,
                                    internal::NrSubmissionAdmissionHandle submissionAdmission) noexcept;
        ~NrSessionSendChannelControl() noexcept;

        std::atomic_size_t refCount_{1};
        std::atomic<NrState> state_{NrState::Open};

        psnr::core::NrSessionKey sessionKey_ = 0;
        NrActorScheduleHandle scheduleHandle_;
        internal::NrSubmissionAdmissionHandle submissionAdmission_;
    };

    class NrSessionSendChannelControlHandle final
    {
    public:
        NrSessionSendChannelControlHandle() noexcept = default;

        NrSessionSendChannelControlHandle(const NrSessionSendChannelControlHandle&) = delete;
        NrSessionSendChannelControlHandle& operator=(const NrSessionSendChannelControlHandle&) = delete;

        NrSessionSendChannelControlHandle(NrSessionSendChannelControlHandle&& other) noexcept;
        NrSessionSendChannelControlHandle& operator=(NrSessionSendChannelControlHandle&& other) noexcept;

        ~NrSessionSendChannelControlHandle() noexcept;

        [[nodiscard]] static psnr::core::NrResult<NrSessionSendChannelControlHandle> Create(
            psnr::core::NrSessionKey sessionKey, const NrActorScheduleHandle& scheduleHandle,
            internal::NrSubmissionAdmissionHandle submissionAdmission) noexcept;
        [[nodiscard]] static NrSessionSendChannelControlHandle Retain(
            NrSessionSendChannelControl& control) noexcept;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] bool IsOpen() const noexcept;
        void MarkClosed() noexcept;

        [[nodiscard]] NrSessionSendChannelControl* Get() const noexcept;

    private:
        explicit NrSessionSendChannelControlHandle(NrSessionSendChannelControl* control) noexcept;
        void Reset() noexcept;

        NrSessionSendChannelControl* control_ = nullptr;
    };
} // namespace psnr::runtime
