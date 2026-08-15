#pragma once

#include "NrServerPressureTypes.h"

#include <cstddef>

namespace psnr::runtime::internal
{
    [[nodiscard]] constexpr bool IsKnownPressureTransactionOutcome(const NrPressureTransactionOutcome outcome) noexcept
    {
        return static_cast<std::size_t>(outcome) < NrPressureTransactionOutcomeCount;
    }

    [[nodiscard]] constexpr bool IsKnownServerMemoryPoolRole(const NrServerMemoryPoolRole role) noexcept
    {
        return static_cast<std::size_t>(role) < NrServerMemoryPoolRoleCount;
    }

    [[nodiscard]] constexpr bool IsKnownPoolPressureOutcome(const NrPoolPressureOutcome outcome) noexcept
    {
        return static_cast<std::size_t>(outcome) < NrPoolPressureOutcomeCount;
    }
} // namespace psnr::runtime::internal
