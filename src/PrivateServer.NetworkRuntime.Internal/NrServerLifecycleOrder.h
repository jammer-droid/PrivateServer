#pragma once

#include "NrLifecycleInternal.h"

#include <span>
#include <vector>

namespace psnr::runtime
{
    class NrServerComponentGraph;

    class NrServerLifecycleOrder final
    {
    public:
        NrServerLifecycleOrder() noexcept = default;

        [[nodiscard]] std::span<INrServerLifecycleComponent* const> Components() noexcept;

    private:
        friend class NrServerComponentGraph;

        void Clear() noexcept;
        [[nodiscard]] NrStatus Add(INrServerLifecycleComponent& component) noexcept;

        std::vector<INrServerLifecycleComponent*> components_;
    };
} // namespace psnr::runtime
