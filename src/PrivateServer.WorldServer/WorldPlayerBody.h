#pragma once

#include "WorldEntityComponents.h"
#include "WorldGrowthSolver.h"

#include <cstdint>

namespace psnr::world
{
    struct WorldPlayerBodyConfig final
    {
        WorldGrowthConfig growth{};
        std::uint32_t maxTrailSampleCount = 0;

        [[nodiscard]] friend bool operator==(const WorldPlayerBodyConfig& left,
                                             const WorldPlayerBodyConfig& right) noexcept = default;
    };

    enum class WorldPlayerBodyUpdateResult : std::uint8_t
    {
        Updated = 0,
        InvalidArgument,
        InvalidConfig,
        InvalidEntityState,
        GrowthSolveFailed,
        AllocationFailed,
        TrailUpdateFailed,
    };

    class WorldPlayerBody final
    {
    public:
        [[nodiscard]] static bool IsEnabled(const WorldPlayerBodyConfig& config) noexcept;
        [[nodiscard]] static bool IsValidConfig(const WorldPlayerBodyConfig& config) noexcept;
        [[nodiscard]] static WorldPlayerBodyUpdateResult Initialize(const WorldPlayerBodyConfig& config,
                                                                    WorldEntityComponents* components) noexcept;
        [[nodiscard]] static WorldPlayerBodyUpdateResult Finalize(const WorldPlayerBodyConfig& config,
                                                                  std::uint32_t growthPoint,
                                                                  WorldEntityComponents* components) noexcept;
    };
} // namespace psnr::world
