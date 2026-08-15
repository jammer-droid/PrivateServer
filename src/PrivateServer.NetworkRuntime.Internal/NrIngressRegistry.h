#pragma once

#include "NrIngress.h"
#include "NrDispatchLane.h"
#include "NrInput.h"
#include "NrResult.h"
#include "NrStatus.h"

#include <array>
#include <cstddef>
#include <span>

namespace psnr::core
{
    struct NrIngressBinding
    {
        NrDispatchLane lane = NrDispatchLane::Count;
        NrIngress* ingress = nullptr;
    };

    class NrIngressRegistry final
    {
    public:
        [[nodiscard]] static NrResult<NrIngressRegistry> Create(
            std::span<const NrIngressBinding> bindings) noexcept;

        [[nodiscard]] NrStatus TryEnqueue(NrDispatchLane lane, NrInput&& input) noexcept;

    private:
        static constexpr std::size_t DispatchLaneCount = static_cast<std::size_t>(NrDispatchLane::Count);

        [[nodiscard]] static bool IsKnownLane(NrDispatchLane lane) noexcept;

    private:
        std::array<NrIngress*, DispatchLaneCount> ingresses_{};
    };

} // namespace psnr::core
