#include "pch.h"

#include "WorldGameplayState.h"

#include "WorldActiveAreaSolver.h"
#include "WorldResourcePopulationSolver.h"

#include <algorithm>
#include <new>
#include <utility>

namespace psnr::world
{
    bool WorldPlayerScoreIdLess::operator()(const WorldPlayerScore& score, const std::uint32_t playerId) const noexcept
    {
        return score.playerId < playerId;
    }

    const WorldPlayerScore* WorldPlayerScoreLookup::FindByPlayerId(const std::span<const WorldPlayerScore> playerScores,
                                                                   const std::uint32_t playerId) noexcept
    {
        const std::span<const WorldPlayerScore>::iterator found =
            std::lower_bound(playerScores.begin(), playerScores.end(), playerId, WorldPlayerScoreIdLess{});
        if (found == playerScores.end() || found->playerId != playerId)
        {
            return nullptr;
        }
        return &*found;
    }

    WorldPlayerScore* WorldPlayerScoreLookup::FindMutableByPlayerId(const std::span<WorldPlayerScore> playerScores,
                                                                    const std::uint32_t playerId) noexcept
    {
        const std::span<WorldPlayerScore>::iterator found =
            std::lower_bound(playerScores.begin(), playerScores.end(), playerId, WorldPlayerScoreIdLess{});
        if (found == playerScores.end() || found->playerId != playerId)
        {
            return nullptr;
        }
        return &*found;
    }

    const WorldPlayerScore* WorldPlayerScoreLookup::FindByEntityKey(
        const std::span<const WorldPlayerScore> playerScores, const WorldEntityKey entityKey) noexcept
    {
        for (const WorldPlayerScore& score : playerScores)
        {
            if (score.controlledEntityKey == entityKey)
            {
                return &score;
            }
        }
        return nullptr;
    }

    WorldResult<WorldGameplayState> CreateWorldGameplayState(const WorldGameplayConfig& config,
                                                             const WorldPhysicsArenaBounds& arenaBounds) noexcept
    {
        if (!IsValid(config, arenaBounds))
        {
            return WorldResult<WorldGameplayState>::Failure(WorldErrorCode::InvalidConfig);
        }

        const WorldActiveAreaConfig activeAreaConfig{
            arenaBounds,
            config.roundDurationTicks,
            config.activeAreaStartRatio,
            config.activeAreaEndRatio,
        };
        WorldResult<WorldActiveArea> startActiveAreaResult = WorldActiveAreaSolver::Solve(activeAreaConfig, 0, 0);
        if (startActiveAreaResult.Failed())
        {
            return WorldResult<WorldGameplayState>::Failure(WorldErrorCode::InvalidConfig);
        }
        WorldResult<WorldResourcePopulationPlan> startPopulationResult =
            WorldResourcePopulationSolver::Solve(startActiveAreaResult.Value(), config.resourceDensityPerUnit2, 0);
        if (startPopulationResult.Failed() || startPopulationResult.Value().targetResourceCount == 0)
        {
            return WorldResult<WorldGameplayState>::Failure(WorldErrorCode::InvalidConfig);
        }
        const WorldResourcePopulationPlan startPopulation = startPopulationResult.TakeValue();

        try
        {
            WorldGameplayState created;
            created.roundState_ = WorldRoundRuntimeState{
                1,
                WorldRoundPhase::Waiting,
                0,
                0,
            };
            created.resourceSlots_.reserve(startPopulation.targetResourceCount);
            for (std::uint32_t slotIndex = 0; slotIndex < startPopulation.targetResourceCount; ++slotIndex)
            {
                created.resourceSlots_.push_back(WorldResourceSlotState{
                    slotIndex + 1,
                    WorldResourceSlotPhase::Dormant,
                });
            }

            return WorldResult<WorldGameplayState>(std::move(created));
        }
        catch (const std::bad_alloc&)
        {
            return WorldResult<WorldGameplayState>::Failure(WorldErrorCode::AllocationFailed);
        }
    }

    WorldPlayerScoreRegisterResult WorldGameplayState::TryRegisterPlayer(
        const std::uint32_t playerId, const WorldEntityKey controlledEntityKey) noexcept
    {
        if (playerId == 0 || !controlledEntityKey.IsValid())
        {
            return WorldPlayerScoreRegisterResult::InvalidArgument;
        }

        const std::vector<WorldPlayerScore>::iterator insertionPoint =
            std::lower_bound(playerScores_.begin(), playerScores_.end(), playerId, WorldPlayerScoreIdLess{});
        if (insertionPoint != playerScores_.end() && insertionPoint->playerId == playerId)
        {
            return insertionPoint->controlledEntityKey == controlledEntityKey
                       ? WorldPlayerScoreRegisterResult::DuplicatePlayer
                       : WorldPlayerScoreRegisterResult::IdentityConflict;
        }

        for (const WorldPlayerScore& score : playerScores_)
        {
            if (score.controlledEntityKey == controlledEntityKey)
            {
                return WorldPlayerScoreRegisterResult::IdentityConflict;
            }
        }

        try
        {
            static_cast<void>(playerScores_.insert(insertionPoint, WorldPlayerScore{playerId, controlledEntityKey, 0}));
            return WorldPlayerScoreRegisterResult::Registered;
        }
        catch (const std::bad_alloc&)
        {
            return WorldPlayerScoreRegisterResult::AllocationFailed;
        }
    }

    bool WorldGameplayState::RemovePlayer(const std::uint32_t playerId,
                                          const WorldEntityKey controlledEntityKey) noexcept
    {
        if (playerId == 0 || !controlledEntityKey.IsValid())
        {
            return false;
        }

        const std::vector<WorldPlayerScore>::iterator found =
            std::lower_bound(playerScores_.begin(), playerScores_.end(), playerId, WorldPlayerScoreIdLess{});
        if (found == playerScores_.end() || found->playerId != playerId ||
            found->controlledEntityKey != controlledEntityKey)
        {
            return false;
        }

        static_cast<void>(playerScores_.erase(found));
        return true;
    }

    bool WorldGameplayState::TryFindPlayerScore(const std::uint32_t playerId,
                                                WorldPlayerScore* const outScore) const noexcept
    {
        if (playerId == 0 || outScore == nullptr)
        {
            return false;
        }

        const WorldPlayerScore* const found = WorldPlayerScoreLookup::FindByPlayerId(PlayerScores(), playerId);
        if (found == nullptr)
        {
            return false;
        }

        *outScore = *found;
        return true;
    }

    const WorldRoundRuntimeState& WorldGameplayState::RoundState() const noexcept
    {
        return roundState_;
    }

    std::span<const WorldPlayerScore> WorldGameplayState::PlayerScores() const noexcept
    {
        return playerScores_;
    }

    std::span<const WorldResourceSlotState> WorldGameplayState::ResourceSlots() const noexcept
    {
        return resourceSlots_;
    }

    const WorldResourceRegistry& WorldGameplayState::ResourceRegistry() const noexcept
    {
        return resourceRegistry_;
    }

    WorldGameplayMetrics WorldGameplayState::Metrics() const noexcept
    {
        return metrics_;
    }

    std::size_t WorldGameplayState::PlayerCount() const noexcept
    {
        return playerScores_.size();
    }
} // namespace psnr::world
