#include "pch.h"

#include "WorldGameplayPhaseResult.h"

#include <utility>

namespace psnr::world
{
    WorldGameplayPhaseResult::WorldGameplayPhaseResult(const std::uint32_t serverTick,
                                                       std::vector<WorldResourcePickup> pickups,
                                                       std::vector<WorldPlayerScoreAward> scoreAwards) noexcept
        : WorldGameplayPhaseResult(serverTick, std::move(pickups), std::move(scoreAwards), {},
                                   WorldGameplayRoundTransition{})
    {
    }

    WorldGameplayPhaseResult::WorldGameplayPhaseResult(const std::uint32_t serverTick,
                                                       std::vector<WorldResourcePickup> pickups,
                                                       std::vector<WorldPlayerScoreAward> scoreAwards,
                                                       std::vector<WorldResourceSpawnRequest> resourceSpawns,
                                                       const WorldGameplayRoundTransition roundTransition,
                                                       std::vector<WorldPlayerBoostCostCommit> boostCosts,
                                                       std::vector<WorldPlayerDeathCommit> playerDeaths,
                                                       std::vector<WorldResourceSpawnRequest> deathDropSpawns,
                                                       const std::uint32_t deathDropPlacementFailureCount,
                                                       std::vector<WorldResourceInstance> outsideResourceRemovals,
                                                       std::vector<WorldDisconnectDropCommit> disconnectDrops) noexcept
        : serverTick_(serverTick)
        , pickups_(std::move(pickups))
        , scoreAwards_(std::move(scoreAwards))
        , boostCosts_(std::move(boostCosts))
        , playerDeaths_(std::move(playerDeaths))
        , disconnectDrops_(std::move(disconnectDrops))
        , deathDropSpawns_(std::move(deathDropSpawns))
        , deathDropPlacementFailureCount_(deathDropPlacementFailureCount)
        , outsideResourceRemovals_(std::move(outsideResourceRemovals))
        , resourceSpawns_(std::move(resourceSpawns))
        , roundTransition_(roundTransition)
    {
    }

    std::uint32_t WorldGameplayPhaseResult::ServerTick() const noexcept
    {
        return serverTick_;
    }

    std::span<const WorldResourcePickup> WorldGameplayPhaseResult::Pickups() const noexcept
    {
        return pickups_;
    }

    std::span<const WorldPlayerScoreAward> WorldGameplayPhaseResult::ScoreAwards() const noexcept
    {
        return scoreAwards_;
    }

    std::span<const WorldPlayerBoostCostCommit> WorldGameplayPhaseResult::BoostCosts() const noexcept
    {
        return boostCosts_;
    }

    std::span<const WorldPlayerDeathCommit> WorldGameplayPhaseResult::PlayerDeaths() const noexcept
    {
        return playerDeaths_;
    }

    std::span<const WorldDisconnectDropCommit> WorldGameplayPhaseResult::DisconnectDrops() const noexcept
    {
        return disconnectDrops_;
    }

    std::span<const WorldResourceSpawnRequest> WorldGameplayPhaseResult::DeathDropSpawns() const noexcept
    {
        return deathDropSpawns_;
    }

    std::uint32_t WorldGameplayPhaseResult::DeathDropPlacementFailureCount() const noexcept
    {
        return deathDropPlacementFailureCount_;
    }

    std::span<const WorldResourceInstance> WorldGameplayPhaseResult::OutsideResourceRemovals() const noexcept
    {
        return outsideResourceRemovals_;
    }

    std::span<const WorldResourceSpawnRequest> WorldGameplayPhaseResult::ResourceSpawns() const noexcept
    {
        return resourceSpawns_;
    }

    const WorldGameplayRoundTransition& WorldGameplayPhaseResult::RoundTransition() const noexcept
    {
        return roundTransition_;
    }
} // namespace psnr::world
