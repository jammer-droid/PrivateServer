#include "pch.h"

#include "NrPacketDispatchTable.h"

#include <new>
#include <utility>

namespace psnr::core
{
    NrResult<NrPacketDispatchTable> NrPacketDispatchTable::Create(std::span<const NrPacketDispatchRule> rules) noexcept
    {
        NrPacketDispatchTable table;

        try
        {
            std::size_t requiredSize = 0;

            for (const NrPacketDispatchRule& rule : rules)
            {
                if (!IsKnownDispatchLane(rule.dispatchLane))
                {
                    return NrResult<NrPacketDispatchTable>::Failure(NrErrorCode::InvalidArgument);
                }

                const std::size_t packetTypeIndex = rule.packetType.value;
                if (requiredSize <= packetTypeIndex)
                {
                    requiredSize = packetTypeIndex + 1;
                }
            }

            table.rules_.resize(requiredSize);

            for (const NrPacketDispatchRule& rule : rules)
            {
                const std::size_t packetTypeIndex = rule.packetType.value;
                if (table.rules_[packetTypeIndex].has_value())
                {
                    return NrResult<NrPacketDispatchTable>::Failure(NrErrorCode::InvalidArgument);
                }

                table.rules_[packetTypeIndex] = rule;
            }
        }
        catch (const std::bad_alloc&)
        {
            return NrResult<NrPacketDispatchTable>::Failure(NrErrorCode::OutOfMemory);
        }

        return NrResult<NrPacketDispatchTable>(std::move(table));
    }

    NrStatus NrPacketDispatchTable::Find(NrPacketType packetType, NrPacketDispatchRule& out) const noexcept
    {
        const std::size_t packetTypeIndex = packetType.value;
        if (packetTypeIndex >= rules_.size() || !rules_[packetTypeIndex].has_value())
        {
            return NrStatus(NrErrorCode::DispatchRuleNotFound);
        }

        out = *rules_[packetTypeIndex];
        return NrStatus();
    }

    bool NrPacketDispatchTable::IsKnownDispatchLane(NrDispatchLane dispatchLane) noexcept
    {
        return static_cast<std::size_t>(dispatchLane) < DispatchLaneCount;
    }
} // namespace psnr::core
