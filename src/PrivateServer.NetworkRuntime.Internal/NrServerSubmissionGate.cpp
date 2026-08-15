#include "pch.h"

#include "NrServerSubmissionGate.h"

#include "NrErrorCode.h"

#include <atomic>
#include <cassert>
#include <cstdint>
#include <new>
#include <utility>

namespace psnr::runtime::internal
{
    using psnr::core::NrErrorCode;
    using psnr::core::NrResult;
    using psnr::core::NrStatus;

    namespace
    {
        using NrAdmissionWord = std::uint64_t;

        // One atomic word linearizes submit admission against server invalidation.
        //
        // bit:   63  62                                              0
        //       +---+-------------------------------------------------+
        //       | I |              in-flight submission count         |
        //       +---+-------------------------------------------------+
        //         ^                         ^
        //         |                         +-- permits currently inside admission
        //         +-- 0: accepting, 1: invalidated (no new permit)
        //
        // Acquire CAS before invalidation: count includes the submit and shutdown waits.
        // Invalidation before acquire CAS: CAS observes I=1 and the submit is rejected.
        inline constexpr NrAdmissionWord NrInvalidatedBit = NrAdmissionWord{1} << 63;
        inline constexpr NrAdmissionWord NrInFlightMask = NrInvalidatedBit - 1;
    } // namespace

    struct NrServerSubmissionGateState final
    {
        void AddRef() noexcept
        {
            refCount.fetch_add(1, std::memory_order_relaxed);
        }

        void ReleaseRef() noexcept
        {
            if (refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
            {
                delete this;
            }
        }

        [[nodiscard]] NrStatus TryAcquire() noexcept
        {
            NrAdmissionWord observed = admissionWord.load(std::memory_order_acquire);
            while (true)
            {
                if ((observed & NrInvalidatedBit) != 0) // invalidbit == 1
                {
                    return NrStatus::Failure(NrErrorCode::InvalidState);
                }

                const NrAdmissionWord inFlight = observed & NrInFlightMask;
                if (inFlight == NrInFlightMask) // full of inFlight
                {
                    return NrStatus::Failure(NrErrorCode::CapacityExceeded);
                }

                const NrAdmissionWord desired = observed + 1;
                if (admissionWord.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                                        std::memory_order_acquire))
                {
                    return NrStatus::Success();
                }
            }
        }

        void ReleasePermit() noexcept
        {
            const NrAdmissionWord previous = admissionWord.fetch_sub(1, std::memory_order_acq_rel);
            assert((previous & NrInFlightMask) != 0);

            if ((previous & NrInFlightMask) == 1) // last submission complete
            {
                admissionWord.notify_all(); // admissionWord를 wait 중인 thread를 wake-up
            }
        }

        void InvalidateAndWait() noexcept
        {
            admissionWord.fetch_or(NrInvalidatedBit, std::memory_order_acq_rel); // mark invalid

            NrAdmissionWord observed = admissionWord.load(std::memory_order_acquire);
            while ((observed & NrInFlightMask) != 0)
            {
                admissionWord.wait(observed, std::memory_order_acquire); // wait last ReleasePermit

                observed = admissionWord.load(std::memory_order_acquire);
            }
        }

        [[nodiscard]] bool IsAccepting() const noexcept
        {
            return (admissionWord.load(std::memory_order_acquire) & NrInvalidatedBit) == 0;
        }

        [[nodiscard]] std::size_t InFlightCount() const noexcept
        {
            return static_cast<std::size_t>(admissionWord.load(std::memory_order_acquire) & NrInFlightMask);
        }

        std::atomic_size_t refCount{1};
        std::atomic<NrAdmissionWord> admissionWord{0};
    };

    NrSubmissionPermit::NrSubmissionPermit(NrSubmissionPermit&& other) noexcept
        : state_(std::exchange(other.state_, nullptr))
    {
    }

    NrSubmissionPermit& NrSubmissionPermit::operator=(NrSubmissionPermit&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            state_ = std::exchange(other.state_, nullptr);
        }

        return *this;
    }

    NrSubmissionPermit::~NrSubmissionPermit() noexcept
    {
        Reset();
    }

    bool NrSubmissionPermit::IsValid() const noexcept
    {
        return state_ != nullptr;
    }

    void NrSubmissionPermit::Reset() noexcept
    {
        NrServerSubmissionGateState* state = std::exchange(state_, nullptr);
        if (state != nullptr)
        {
            state->ReleasePermit();
            state->ReleaseRef();
        }
    }

    NrSubmissionPermit::NrSubmissionPermit(NrServerSubmissionGateState* state) noexcept
        : state_(state)
    {
    }

    NrSubmissionAdmissionHandle::NrSubmissionAdmissionHandle(const NrSubmissionAdmissionHandle& other) noexcept
        : state_(other.state_)
    {
        if (state_ != nullptr)
        {
            state_->AddRef();
        }
    }

    NrSubmissionAdmissionHandle& NrSubmissionAdmissionHandle::operator=(
        const NrSubmissionAdmissionHandle& other) noexcept
    {
        if (this != &other)
        {
            NrServerSubmissionGateState* nextState = other.state_;
            if (nextState != nullptr)
            {
                nextState->AddRef();
            }

            Reset();
            state_ = nextState;
        }

        return *this;
    }

    NrSubmissionAdmissionHandle::NrSubmissionAdmissionHandle(NrSubmissionAdmissionHandle&& other) noexcept
        : state_(std::exchange(other.state_, nullptr))
    {
    }

    NrSubmissionAdmissionHandle& NrSubmissionAdmissionHandle::operator=(NrSubmissionAdmissionHandle&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            state_ = std::exchange(other.state_, nullptr);
        }

        return *this;
    }

    NrSubmissionAdmissionHandle::~NrSubmissionAdmissionHandle() noexcept
    {
        Reset();
    }

    bool NrSubmissionAdmissionHandle::IsValid() const noexcept
    {
        return state_ != nullptr;
    }

    bool NrSubmissionAdmissionHandle::IsAccepting() const noexcept
    {
        return state_ != nullptr && state_->IsAccepting();
    }

    std::size_t NrSubmissionAdmissionHandle::InFlightCount() const noexcept
    {
        return state_ == nullptr ? 0 : state_->InFlightCount();
    }

    bool NrSubmissionAdmissionHandle::MatchesPermit(const NrSubmissionPermit& permit) const noexcept
    {
        return state_ != nullptr && state_ == permit.state_;
    }

    NrStatus NrSubmissionAdmissionHandle::TryAcquirePermit(NrSubmissionPermit& outPermit) const noexcept
    {
        if (state_ == nullptr || outPermit.IsValid())
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        const NrStatus acquireStatus = state_->TryAcquire();
        if (acquireStatus.Failed())
        {
            return acquireStatus;
        }

        state_->AddRef();
        outPermit = NrSubmissionPermit(state_);
        return NrStatus::Success();
    }

    void NrSubmissionAdmissionHandle::Reset() noexcept
    {
        NrServerSubmissionGateState* state = std::exchange(state_, nullptr);
        if (state != nullptr)
        {
            state->ReleaseRef();
        }
    }

    NrSubmissionAdmissionHandle::NrSubmissionAdmissionHandle(NrServerSubmissionGateState* state) noexcept
        : state_(state)
    {
    }

    NrServerSubmissionGate::NrServerSubmissionGate(NrServerSubmissionGate&& other) noexcept
        : state_(std::exchange(other.state_, nullptr))
    {
    }

    NrServerSubmissionGate& NrServerSubmissionGate::operator=(NrServerSubmissionGate&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            state_ = std::exchange(other.state_, nullptr);
        }

        return *this;
    }

    NrServerSubmissionGate::~NrServerSubmissionGate() noexcept
    {
        Reset();
    }

    NrResult<NrServerSubmissionGate> NrServerSubmissionGate::Create() noexcept
    {
        NrServerSubmissionGateState* state = new (std::nothrow) NrServerSubmissionGateState();
        if (state == nullptr)
        {
            return NrResult<NrServerSubmissionGate>::Failure(NrErrorCode::OutOfMemory);
        }

        return NrResult<NrServerSubmissionGate>(NrServerSubmissionGate(state));
    }

    bool NrServerSubmissionGate::IsValid() const noexcept
    {
        return state_ != nullptr;
    }

    bool NrServerSubmissionGate::IsAccepting() const noexcept
    {
        return state_ != nullptr && state_->IsAccepting();
    }

    std::size_t NrServerSubmissionGate::InFlightCount() const noexcept
    {
        return state_ == nullptr ? 0 : state_->InFlightCount();
    }

    NrSubmissionAdmissionHandle NrServerSubmissionGate::CreateAdmissionHandle() const noexcept
    {
        if (state_ == nullptr)
        {
            return NrSubmissionAdmissionHandle();
        }

        state_->AddRef();
        return NrSubmissionAdmissionHandle(state_);
    }

    NrStatus NrServerSubmissionGate::InvalidateAndWait() const noexcept
    {
        if (state_ == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        state_->InvalidateAndWait();
        return NrStatus::Success();
    }

    void NrServerSubmissionGate::Reset() noexcept
    {
        NrServerSubmissionGateState* state = std::exchange(state_, nullptr);
        if (state != nullptr)
        {
            state->InvalidateAndWait();
            state->ReleaseRef();
        }
    }

    NrServerSubmissionGate::NrServerSubmissionGate(NrServerSubmissionGateState* state) noexcept
        : state_(state)
    {
    }
} // namespace psnr::runtime::internal
