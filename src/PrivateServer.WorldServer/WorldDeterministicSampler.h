#pragma once

#include "WorldResult.h"

#include <cstdint>

namespace psnr::world
{
    enum class WorldDeterministicSampleDomain : std::uint8_t
    {
        Invalid = 0,
        PlayerSpawn,
        AmbientResourceSpawn,
    };

    class WorldDeterministicSampler final
    {
    public:
        [[nodiscard]] static WorldResult<float> SampleUnit(WorldDeterministicSampleDomain domain,
                                                           std::uint32_t serverTick, std::uint32_t subjectId,
                                                           std::uint32_t candidateOrdinal,
                                                           std::uint32_t sampleChannel) noexcept;
    };
} // namespace psnr::world
