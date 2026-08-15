#pragma once

#include "WorldOverviewSnapshot.h"
#include "WorldResult.h"

#include <cstdint>
#include <string>
#include <vector>

namespace psnr::world
{
    struct WorldOverviewPlayerInput final
    {
        std::uint32_t playerId = 0;
        std::uint32_t growthPoint = 0;
        std::vector<protocol::v3::WorldOverviewPoint> bodySamples;
        std::string displayName;
    };

    struct WorldOverviewPlanInput final
    {
        std::uint32_t serverTick = 0;
        std::uint32_t overviewId = 0;
        float mapMinX = 0.0f;
        float mapMinY = 0.0f;
        float mapMaxX = 0.0f;
        float mapMaxY = 0.0f;
        float activeAreaCenterX = 0.0f;
        float activeAreaCenterY = 0.0f;
        float activeAreaRadius = 0.0f;
        std::vector<WorldOverviewPlayerInput> alivePlayers;
    };

    // 이번 tick 에 overview 발행 여부 결정
    struct WorldOverviewCadenceDecision final
    {
        std::uint32_t overviewId = 0;
        std::uint32_t suppressedOverviewCount = 0; // 최신 상태 발행을 위해 생략한 overview 개수

        [[nodiscard]] bool IsDue() const noexcept
        {
            return overviewId != 0;
        }
    };

    enum class WorldOverviewCadenceResult : std::uint8_t
    {
        Evaluated,
        InvalidArgument,
        InvalidInput,
        IdExhausted,
    };

    class WorldOverviewCadence final
    {
    public:
        [[nodiscard]] WorldOverviewCadenceResult Evaluate(std::uint32_t firstProcessedServerTick,
                                                          std::uint32_t lastProcessedServerTick,
                                                          WorldOverviewCadenceDecision* outDecision) noexcept;

    private:
        friend WorldResult<WorldOverviewCadence> CreateWorldOverviewCadence(std::uint32_t worldTickRateHz,
                                                                            std::uint32_t currentServerTick) noexcept;

        static constexpr std::uint32_t OverviewRateHz = 2;

        std::uint64_t intervalTicks_ = 0;
        std::uint64_t nextDueTick_ = 0;
        std::uint64_t nextOverviewId_ = 1;
    };

    [[nodiscard]] WorldResult<WorldOverviewCadence> CreateWorldOverviewCadence(
        std::uint32_t worldTickRateHz, std::uint32_t currentServerTick) noexcept;

    class WorldOverviewPlanner final
    {
    public:
        [[nodiscard]] WorldResult<std::vector<protocol::v3::WorldOverviewSnapshot>> Plan(
            const WorldOverviewPlanInput& input) const noexcept;
    };
} // namespace psnr::world
