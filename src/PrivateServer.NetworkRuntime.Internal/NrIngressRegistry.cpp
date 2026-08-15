#include "pch.h"

#include "NrIngressRegistry.h"

#include <utility>

namespace psnr::core
{
    NrResult<NrIngressRegistry> NrIngressRegistry::Create(std::span<const NrIngressBinding> bindings) noexcept
    {
        NrIngressRegistry registry;

        for (const NrIngressBinding& binding : bindings)
        {
            if (!IsKnownLane(binding.lane) || binding.ingress == nullptr)
            {
                return NrResult<NrIngressRegistry>::Failure(NrErrorCode::InvalidArgument);
            }

            const std::size_t laneIndex = static_cast<std::size_t>(binding.lane);
            if (registry.ingresses_[laneIndex] != nullptr)
            {
                return NrResult<NrIngressRegistry>::Failure(NrErrorCode::InvalidArgument);
            }

            registry.ingresses_[laneIndex] = binding.ingress;
        }

        return NrResult<NrIngressRegistry>(std::move(registry));
    }

    NrStatus NrIngressRegistry::TryEnqueue(NrDispatchLane lane, NrInput&& input) noexcept
    {
        if (!IsKnownLane(lane))
        {
            return NrStatus(NrErrorCode::InvalidArgument);
        }

        NrIngress* ingress = ingresses_[static_cast<std::size_t>(lane)];
        if (ingress == nullptr)
        {
            return NrStatus(NrErrorCode::InvalidState);
        }

        return ingress->TryEnqueue(std::move(input));
    }

    bool NrIngressRegistry::IsKnownLane(NrDispatchLane lane) noexcept
    {
        return static_cast<std::size_t>(lane) < DispatchLaneCount;
    }
} // namespace psnr::core
