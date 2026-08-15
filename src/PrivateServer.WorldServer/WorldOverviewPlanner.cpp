#include "pch.h"

#include "WorldOverviewPlanner.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <new>
#include <utility>

namespace psnr::world
{
    namespace
    {
        constexpr double SampleSpacing = 1.0;
        constexpr std::size_t MaximumSamplesPerRecord =
            (protocol::v3::WorldOverviewSnapshot::Wire::MaximumPayloadBytes -
             protocol::v3::WorldOverviewSnapshot::Wire::HeaderBytes -
             protocol::v3::WorldOverviewSnapshot::Wire::PlayerHeaderBytes) /
            protocol::v3::WorldOverviewSnapshot::Wire::PointBytes;

        [[nodiscard]] bool IsValidPoint(const protocol::v3::WorldOverviewPoint& point) noexcept
        {
            return std::isfinite(point.positionX) && std::isfinite(point.positionY);
        }

        [[nodiscard]] bool IsValidInput(const WorldOverviewPlanInput& input) noexcept
        {
            if (input.overviewId == 0 || !std::isfinite(input.mapMinX) || !std::isfinite(input.mapMinY) ||
                !std::isfinite(input.mapMaxX) || !std::isfinite(input.mapMaxY) || input.mapMinX >= input.mapMaxX ||
                input.mapMinY >= input.mapMaxY || !std::isfinite(input.activeAreaCenterX) ||
                !std::isfinite(input.activeAreaCenterY) || !std::isfinite(input.activeAreaRadius) ||
                input.activeAreaRadius <= 0.0f || input.alivePlayers.size() > std::numeric_limits<std::uint16_t>::max())
            {
                return false;
            }
            for (const WorldOverviewPlayerInput& player : input.alivePlayers)
            {
                if (player.playerId == 0 || player.bodySamples.empty() ||
                    !protocol::WorldProtocolWireCodec::IsValidPlayerDisplayName(player.displayName))
                {
                    return false;
                }
                for (const protocol::v3::WorldOverviewPoint& point : player.bodySamples)
                {
                    if (!IsValidPoint(point))
                    {
                        return false;
                    }
                }
            }
            return true;
        }

        [[nodiscard]] WorldResult<void> Downsample(const std::vector<protocol::v3::WorldOverviewPoint>& source,
                                                   std::vector<protocol::v3::WorldOverviewPoint>* const outSamples)
        {
            outSamples->clear();
            outSamples->push_back(source.front());
            double traversed = 0.0;
            double nextTarget = SampleSpacing;
            for (std::size_t index = 1; index < source.size(); ++index)
            {
                const protocol::v3::WorldOverviewPoint& start = source[index - 1];
                const protocol::v3::WorldOverviewPoint& end = source[index];
                const double deltaX = static_cast<double>(end.positionX) - start.positionX;
                const double deltaY = static_cast<double>(end.positionY) - start.positionY;
                const double segmentLength = std::hypot(deltaX, deltaY);
                if (segmentLength == 0.0)
                {
                    continue;
                }
                while (nextTarget <= traversed + segmentLength)
                {
                    if (outSamples->size() >= MaximumSamplesPerRecord)
                    {
                        return WorldResult<void>::Failure(WorldErrorCode::CapacityExceeded);
                    }
                    const double ratio = (nextTarget - traversed) / segmentLength;
                    outSamples->push_back(protocol::v3::WorldOverviewPoint{
                        static_cast<float>(start.positionX + deltaX * ratio),
                        static_cast<float>(start.positionY + deltaY * ratio),
                    });
                    nextTarget += SampleSpacing;
                }
                traversed += segmentLength;
            }

            const protocol::v3::WorldOverviewPoint& tail = source.back();
            if (outSamples->back() != tail)
            {
                if (outSamples->size() >= MaximumSamplesPerRecord)
                {
                    return WorldResult<void>::Failure(WorldErrorCode::CapacityExceeded);
                }
                outSamples->push_back(tail);
            }
            return WorldResult<void>::Success();
        }

        void CopyMetadata(const WorldOverviewPlanInput& input,
                          protocol::v3::WorldOverviewSnapshot* const chunk) noexcept
        {
            chunk->serverTick = input.serverTick;
            chunk->overviewId = input.overviewId;
            chunk->mapMinX = input.mapMinX;
            chunk->mapMinY = input.mapMinY;
            chunk->mapMaxX = input.mapMaxX;
            chunk->mapMaxY = input.mapMaxY;
            chunk->activeAreaCenterX = input.activeAreaCenterX;
            chunk->activeAreaCenterY = input.activeAreaCenterY;
            chunk->activeAreaRadius = input.activeAreaRadius;
        }
    } // namespace

    WorldResult<WorldOverviewCadence> CreateWorldOverviewCadence(const std::uint32_t worldTickRateHz,
                                                                 const std::uint32_t currentServerTick) noexcept
    {
        if (worldTickRateHz == 0 || worldTickRateHz % WorldOverviewCadence::OverviewRateHz != 0)
        {
            return WorldResult<WorldOverviewCadence>::Failure(WorldErrorCode::InvalidConfig);
        }

        WorldOverviewCadence built;
        built.intervalTicks_ = worldTickRateHz / WorldOverviewCadence::OverviewRateHz;
        const std::uint64_t remainder = currentServerTick % built.intervalTicks_;
        built.nextDueTick_ =
            static_cast<std::uint64_t>(currentServerTick) + (remainder == 0 ? 0 : built.intervalTicks_ - remainder);
        return WorldResult<WorldOverviewCadence>(built);
    }

    // overview 는 server의 batch tick 처리가 진행된 다음 평가
    // firstProcessedServerTick: server에서 처리한 batch 의 첫 tick
    // lastProcessedServerTick: server에서 처리한 batch의 마지막 tick
    WorldOverviewCadenceResult WorldOverviewCadence::Evaluate(const std::uint32_t firstProcessedServerTick,
                                                              const std::uint32_t lastProcessedServerTick,
                                                              WorldOverviewCadenceDecision* const outDecision) noexcept
    {
        if (outDecision == nullptr)
        {
            return WorldOverviewCadenceResult::InvalidArgument;
        }
        if (intervalTicks_ == 0 || firstProcessedServerTick > lastProcessedServerTick)
        {
            return WorldOverviewCadenceResult::InvalidInput;
        }

        WorldOverviewCadenceDecision decision;
        while (nextDueTick_ < firstProcessedServerTick) // 이미 지나간 tick 은 생략
        {
            nextDueTick_ += intervalTicks_;
        }
        if (nextDueTick_ > lastProcessedServerTick) // 다음 overview 발행 시점이 이번 batch 를 넘어가는 경우
        {
            *outDecision = decision;
            return WorldOverviewCadenceResult::Evaluated;
        }
        if (nextOverviewId_ > std::numeric_limits<std::uint32_t>::max())
        {
            return WorldOverviewCadenceResult::IdExhausted;
        }

        // 현재 범위의 due tick 계산
        const std::uint64_t dueCount =
            (static_cast<std::uint64_t>(lastProcessedServerTick) - nextDueTick_) / intervalTicks_ + 1;
        decision.overviewId = static_cast<std::uint32_t>(nextOverviewId_);
        // 최신 overview 를 제외하고 생략한 개수
        decision.suppressedOverviewCount = static_cast<std::uint32_t>(dueCount - 1);

        nextDueTick_ += dueCount * intervalTicks_;
        ++nextOverviewId_;
        *outDecision = decision;
        return WorldOverviewCadenceResult::Evaluated;
    }

    WorldResult<std::vector<protocol::v3::WorldOverviewSnapshot>> WorldOverviewPlanner::Plan(
        const WorldOverviewPlanInput& input) const noexcept
    {
        if (!IsValidInput(input))
        {
            return WorldResult<std::vector<protocol::v3::WorldOverviewSnapshot>>::Failure(WorldErrorCode::InvalidInput);
        }

        try
        {
            std::vector<std::size_t> playerOrder(input.alivePlayers.size());
            for (std::size_t index = 0; index < playerOrder.size(); ++index)
            {
                playerOrder[index] = index;
            }
            std::sort(playerOrder.begin(), playerOrder.end(), [&input](const std::size_t left, const std::size_t right)
                      { return input.alivePlayers[left].playerId < input.alivePlayers[right].playerId; });
            for (std::size_t index = 1; index < playerOrder.size(); ++index)
            {
                if (input.alivePlayers[playerOrder[index - 1]].playerId ==
                    input.alivePlayers[playerOrder[index]].playerId)
                {
                    return WorldResult<std::vector<protocol::v3::WorldOverviewSnapshot>>::Failure(
                        WorldErrorCode::InvalidInput);
                }
            }

            std::vector<protocol::v3::WorldOverviewPlayer> players;
            players.reserve(playerOrder.size());
            for (const std::size_t sourceIndex : playerOrder)
            {
                const WorldOverviewPlayerInput& source = input.alivePlayers[sourceIndex];
                protocol::v3::WorldOverviewPlayer built;
                built.playerId = source.playerId;
                built.growthPoint = source.growthPoint;
                const WorldResult<void> sampleResult = Downsample(source.bodySamples, &built.bodySamples);
                if (sampleResult.Failed())
                {
                    return WorldResult<std::vector<protocol::v3::WorldOverviewSnapshot>>::Failure(sampleResult.Error());
                }
                players.push_back(std::move(built));
            }

            std::vector<std::size_t> rankOrder = playerOrder;
            std::sort(rankOrder.begin(), rankOrder.end(),
                      [&input](const std::size_t left, const std::size_t right)
                      {
                          const WorldOverviewPlayerInput& leftPlayer = input.alivePlayers[left];
                          const WorldOverviewPlayerInput& rightPlayer = input.alivePlayers[right];
                          return leftPlayer.growthPoint != rightPlayer.growthPoint
                                     ? leftPlayer.growthPoint > rightPlayer.growthPoint
                                     : leftPlayer.playerId < rightPlayer.playerId;
                      });

            protocol::v3::WorldOverviewSnapshot current;
            CopyMetadata(input, &current);
            const std::size_t leaderboardCount =
                std::min(rankOrder.size(), protocol::v3::WorldOverviewSnapshot::Wire::MaximumLeaderboardEntryCount);
            current.leaderboard.reserve(leaderboardCount);
            std::uint16_t rank = 0;
            std::uint32_t previousGrowth = 0;
            for (std::size_t index = 0; index < leaderboardCount; ++index)
            {
                const WorldOverviewPlayerInput& player = input.alivePlayers[rankOrder[index]];
                if (index == 0 || player.growthPoint != previousGrowth)
                {
                    rank = static_cast<std::uint16_t>(index + 1);
                    previousGrowth = player.growthPoint;
                }
                current.leaderboard.push_back(protocol::v3::WorldOverviewLeaderboardEntry{
                    rank,
                    player.playerId,
                    player.growthPoint,
                    player.displayName,
                });
            }

            std::vector<protocol::v3::WorldOverviewSnapshot> chunks;
            for (protocol::v3::WorldOverviewPlayer& player : players)
            {
                // A player record is the atomic chunking unit: its header and all downsampled body points
                // must fit in one payload. The first chunk has less room because it alone owns the leaderboard.
                const std::size_t recordBytes =
                    protocol::v3::WorldOverviewSnapshot::Wire::PlayerHeaderBytes +
                    player.bodySamples.size() * protocol::v3::WorldOverviewSnapshot::Wire::PointBytes;
                std::size_t currentBytes = protocol::v3::WorldOverviewSnapshot::CalculatePayloadBytes(current);
                if (recordBytes > protocol::v3::WorldOverviewSnapshot::Wire::MaximumPayloadBytes -
                                      protocol::v3::WorldOverviewSnapshot::Wire::HeaderBytes)
                {
                    return WorldResult<std::vector<protocol::v3::WorldOverviewSnapshot>>::Failure(
                        WorldErrorCode::CapacityExceeded);
                }
                if (recordBytes > protocol::v3::WorldOverviewSnapshot::Wire::MaximumPayloadBytes - currentBytes)
                {
                    // Seal the current chunk and start a metadata-only chunk. Deliberately do not copy the
                    // leaderboard: the wire contract permits it only in chunk zero.
                    chunks.push_back(std::move(current));
                    current = protocol::v3::WorldOverviewSnapshot{};
                    CopyMetadata(input, &current);
                    currentBytes = protocol::v3::WorldOverviewSnapshot::CalculatePayloadBytes(current);
                }
                if (recordBytes > protocol::v3::WorldOverviewSnapshot::Wire::MaximumPayloadBytes - currentBytes)
                {
                    return WorldResult<std::vector<protocol::v3::WorldOverviewSnapshot>>::Failure(
                        WorldErrorCode::CapacityExceeded);
                }
                current.players.push_back(std::move(player));
            }
            chunks.push_back(std::move(current));
            if (chunks.size() > std::numeric_limits<std::uint16_t>::max())
            {
                return WorldResult<std::vector<protocol::v3::WorldOverviewSnapshot>>::Failure(
                    WorldErrorCode::CapacityExceeded);
            }
            // The total is known only after packing, so assign the assembly coordinates in one final pass.
            const std::uint16_t chunkCount = static_cast<std::uint16_t>(chunks.size());
            for (std::size_t index = 0; index < chunks.size(); ++index)
            {
                chunks[index].chunkIndex = static_cast<std::uint16_t>(index);
                chunks[index].chunkCount = chunkCount;
            }

            return WorldResult<std::vector<protocol::v3::WorldOverviewSnapshot>>(std::move(chunks));
        }
        catch (const std::bad_alloc&)
        {
            return WorldResult<std::vector<protocol::v3::WorldOverviewSnapshot>>::Failure(
                WorldErrorCode::AllocationFailed);
        }
    }
} // namespace psnr::world
