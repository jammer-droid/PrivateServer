#pragma once

#include "NrPacketDispatchRule.h"
#include "NrResult.h"
#include "NrStatus.h"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace psnr::core
{
    class NrPacketDispatchTable
    {
    public:
        [[nodiscard]] static NrResult<NrPacketDispatchTable> Create(
            std::span<const NrPacketDispatchRule> rules) noexcept;

        [[nodiscard]] NrStatus Find(NrPacketType packetType, NrPacketDispatchRule& out) const noexcept;

    private:
        static constexpr std::size_t DispatchLaneCount = static_cast<std::size_t>(NrDispatchLane::Count);

        [[nodiscard]] static bool IsKnownDispatchLane(NrDispatchLane dispatchLane) noexcept;

    private:
        std::vector<std::optional<NrPacketDispatchRule>> rules_;
    };

} // namespace psnr::core
