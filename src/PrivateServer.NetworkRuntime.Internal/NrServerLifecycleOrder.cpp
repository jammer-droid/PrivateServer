#include "pch.h"

#include "NrServerLifecycleOrder.h"

#include "NrErrorCode.h"

#include <new>

namespace psnr::runtime
{
    using psnr::core::NrErrorCode;

    std::span<INrServerLifecycleComponent* const> NrServerLifecycleOrder::Components() noexcept
    {
        return components_;
    }

    void NrServerLifecycleOrder::Clear() noexcept
    {
        components_.clear();
    }

    NrStatus NrServerLifecycleOrder::Add(INrServerLifecycleComponent& component) noexcept
    {
        try
        {
            components_.push_back(&component);
        }
        catch (const std::bad_alloc&)
        {
            return NrStatus::Failure(NrErrorCode::OutOfMemory);
        }

        return NrStatus::Success();
    }
} // namespace psnr::runtime
