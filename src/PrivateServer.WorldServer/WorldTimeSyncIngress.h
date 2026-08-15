#pragma once

#include "WorldIngressAdmission.h"
#include "WorldTimeSyncResponse.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace psnr::world
{
    class WorldTimeSyncIngress final
    {
    public:
        [[nodiscard]] static WorldIngressAdmissionResult Admit(
            const WorldIngressAdmissionContext& context, std::uint32_t lastCompletedServerTick,
            std::span<const std::byte> payload, protocol::v1::WorldTimeSyncResponse* outResponse) noexcept;
    };
} // namespace psnr::world
