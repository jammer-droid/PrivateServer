#pragma once

#include "NrResult.h"
#include "NrStatus.h"

#include <cstddef>

namespace psnr::runtime::internal
{
    struct NrServerSubmissionGateState;
    class NrSubmissionAdmissionHandle;

    class NrSubmissionPermit final
    {
    public:
        NrSubmissionPermit() noexcept = default;

        NrSubmissionPermit(const NrSubmissionPermit&) = delete;
        NrSubmissionPermit& operator=(const NrSubmissionPermit&) = delete;

        NrSubmissionPermit(NrSubmissionPermit&& other) noexcept;
        NrSubmissionPermit& operator=(NrSubmissionPermit&& other) noexcept;

        ~NrSubmissionPermit() noexcept;

        [[nodiscard]] bool IsValid() const noexcept;
        void Reset() noexcept;

    private:
        friend class NrSubmissionAdmissionHandle;

        explicit NrSubmissionPermit(NrServerSubmissionGateState* state) noexcept;

        NrServerSubmissionGateState* state_ = nullptr;
    };

    class NrSubmissionAdmissionHandle final
    {
    public:
        NrSubmissionAdmissionHandle() noexcept = default;

        NrSubmissionAdmissionHandle(const NrSubmissionAdmissionHandle& other) noexcept;
        NrSubmissionAdmissionHandle& operator=(const NrSubmissionAdmissionHandle& other) noexcept;

        NrSubmissionAdmissionHandle(NrSubmissionAdmissionHandle&& other) noexcept;
        NrSubmissionAdmissionHandle& operator=(NrSubmissionAdmissionHandle&& other) noexcept;

        ~NrSubmissionAdmissionHandle() noexcept;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] bool IsAccepting() const noexcept;
        [[nodiscard]] std::size_t InFlightCount() const noexcept;
        [[nodiscard]] bool MatchesPermit(const NrSubmissionPermit& permit) const noexcept;

        [[nodiscard]] psnr::core::NrStatus TryAcquirePermit(NrSubmissionPermit& outPermit) const noexcept;

        void Reset() noexcept;

    private:
        friend class NrServerSubmissionGate;

        explicit NrSubmissionAdmissionHandle(NrServerSubmissionGateState* state) noexcept;

        NrServerSubmissionGateState* state_ = nullptr;
    };

    class NrServerSubmissionGate final
    {
    public:
        NrServerSubmissionGate() noexcept = default;

        NrServerSubmissionGate(const NrServerSubmissionGate&) = delete;
        NrServerSubmissionGate& operator=(const NrServerSubmissionGate&) = delete;

        NrServerSubmissionGate(NrServerSubmissionGate&& other) noexcept;
        NrServerSubmissionGate& operator=(NrServerSubmissionGate&& other) noexcept;

        ~NrServerSubmissionGate() noexcept;

        [[nodiscard]] static psnr::core::NrResult<NrServerSubmissionGate> Create() noexcept;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] bool IsAccepting() const noexcept;
        [[nodiscard]] std::size_t InFlightCount() const noexcept;
        [[nodiscard]] NrSubmissionAdmissionHandle CreateAdmissionHandle() const noexcept;
        [[nodiscard]] psnr::core::NrStatus InvalidateAndWait() const noexcept;

        void Reset() noexcept;

    private:
        explicit NrServerSubmissionGate(NrServerSubmissionGateState* state) noexcept;

        NrServerSubmissionGateState* state_ = nullptr;
    };
} // namespace psnr::runtime::internal
