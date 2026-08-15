#include "pch.h"

#include "WorldAoiPlanner.h"

#include <algorithm>
#include <iterator>
#include <new>
#include <utility>
#include <vector>

namespace psnr::world
{
    namespace
    {
        struct PendingVisibleSet final
        {
            WorldSessionKey sessionKey{};
            std::vector<WorldEntityKey> entityKeys;
            bool replacesExisting = false;
        };

        [[nodiscard]] bool RecipientLess(const WorldAoiRecipient& left, const WorldAoiRecipient& right) noexcept
        {
            return left.sessionKey.value < right.sessionKey.value;
        }

        [[nodiscard]] bool PrunedVisibilityLess(const WorldAoiPrunedVisibility& left,
                                                const WorldAoiPrunedVisibility& right) noexcept
        {
            if (left.sessionKey != right.sessionKey)
            {
                return left.sessionKey.value < right.sessionKey.value;
            }
            return left.entityKey < right.entityKey;
        }
    } // namespace

    WorldAoiPlanResult WorldAoiPlanner::PlanAndCommit(const std::span<const WorldAoiRecipient> recipients,
                                                      const WorldSpatialIndex& spatialIndex,
                                                      std::vector<WorldAoiVisibilityDiff>* const outDiffs) noexcept
    {
        if (outDiffs == nullptr)
        {
            return WorldAoiPlanResult::InvalidArgument;
        }

        try
        {
            std::vector<WorldAoiRecipient> orderedRecipients{recipients.begin(), recipients.end()};
            std::sort(orderedRecipients.begin(), orderedRecipients.end(), RecipientLess);
            for (std::size_t index = 0; index < orderedRecipients.size(); ++index)
            {
                if (!orderedRecipients[index].sessionKey.IsValid() ||
                    !orderedRecipients[index].controlledEntityKey.IsValid())
                {
                    return WorldAoiPlanResult::InvalidInput;
                }

                if (index > 0 && orderedRecipients[index - 1].sessionKey == orderedRecipients[index].sessionKey)
                {
                    return WorldAoiPlanResult::InvalidInput;
                }
            }

            std::vector<WorldAoiVisibilityDiff> diffs;
            diffs.reserve(orderedRecipients.size());
            std::vector<PendingVisibleSet> pendingVisibleSets;
            pendingVisibleSets.reserve(orderedRecipients.size());

            for (const WorldAoiRecipient& recipient : orderedRecipients)
            {
                std::vector<WorldEntityKey> enteredRadiusVisible;  // enter radius 내부
                std::vector<WorldEntityKey> retainedRadiusVisible; // retain radius 내부
                const WorldSpatialQueryResult queryResult = spatialIndex.QueryVisibilityBands(
                    recipient.controlledEntityKey, &enteredRadiusVisible, &retainedRadiusVisible);
                if (queryResult == WorldSpatialQueryResult::ObserverNotFound ||
                    queryResult == WorldSpatialQueryResult::InvalidArgument)
                {
                    return WorldAoiPlanResult::InvalidInput;
                }
                if (queryResult == WorldSpatialQueryResult::AllocationFailed)
                {
                    return WorldAoiPlanResult::AllocationFailed;
                }

                const VisibleSetMap::const_iterator previousIterator = visibleSets_.find(recipient.sessionKey);
                const std::span<const WorldEntityKey> previousVisible =
                    previousIterator == visibleSets_.end() ? std::span<const WorldEntityKey>{}
                                                           : std::span<const WorldEntityKey>{previousIterator->second};

                WorldAoiVisibilityDiff diff;
                diff.sessionKey = recipient.sessionKey;

                // entered 범위 - prevVisible = prevVisible 말고 새로 들어온 entity
                std::set_difference(enteredRadiusVisible.begin(), enteredRadiusVisible.end(), previousVisible.begin(),
                                    previousVisible.end(), std::back_inserter(diff.entered));

                // retained 범위 & prevVisible = 기존 들어온 entity 포함해서
                // retianed 범위 내부이면 stay
                std::set_intersection(retainedRadiusVisible.begin(), retainedRadiusVisible.end(),
                                      previousVisible.begin(), previousVisible.end(), std::back_inserter(diff.stayed));

                // prevVisible 범위 - retained 범위 = 이전에는 있었는데, 현재 retained 에는 없음
                // left 처리
                std::set_difference(previousVisible.begin(), previousVisible.end(), retainedRadiusVisible.begin(),
                                    retainedRadiusVisible.end(), std::back_inserter(diff.left));

                std::vector<WorldEntityKey> currentVisible;
                currentVisible.reserve(diff.entered.size() + diff.stayed.size());
                std::set_union(diff.entered.begin(), diff.entered.end(), diff.stayed.begin(), diff.stayed.end(),
                               std::back_inserter(currentVisible));

                pendingVisibleSets.push_back(PendingVisibleSet{
                    recipient.sessionKey,
                    std::move(currentVisible),
                    previousIterator != visibleSets_.end(),
                });
                diffs.push_back(std::move(diff));
            }

            VisibleSetMap newVisibleSets;
            newVisibleSets.reserve(pendingVisibleSets.size());
            for (PendingVisibleSet& pending : pendingVisibleSets)
            {
                if (!pending.replacesExisting)
                {
                    newVisibleSets.emplace(pending.sessionKey, std::move(pending.entityKeys));
                }
            }
            visibleSets_.reserve(visibleSets_.size() + newVisibleSets.size());
            for (PendingVisibleSet& pending : pendingVisibleSets)
            {
                if (pending.replacesExisting)
                {
                    VisibleSetMap::iterator existing = visibleSets_.find(pending.sessionKey);
                    existing->second.swap(pending.entityKeys);
                }
            }
            visibleSets_.merge(newVisibleSets);
            *outDiffs = std::move(diffs);
            return WorldAoiPlanResult::Planned;
        }
        catch (...)
        {
            return WorldAoiPlanResult::AllocationFailed;
        }
    }

    // gameplay 에서 제거된 entity 를 AOI visible set에서 제거
    //      - AOI 에서 계산되기 전에 이미 EntityRemove.Collected 로 제거됨
    //      - AOI 계산으로 인해 EntityRemove.LeftAoi 중복 packet 방지
    // 제거된 entity 를 보고 있던 session 정보 반환
    WorldAoiPlanResult WorldAoiPlanner::PruneVisibleEntities(
        const std::span<const WorldEntityKey> entityKeys,                  // gameplay 에서 제거된 entity 목록
        std::vector<WorldAoiPrunedVisibility>* const outPrunedVisibilities // 제거된 entity 를 보던 session
        ) noexcept
    {
        if (outPrunedVisibilities == nullptr)
        {
            return WorldAoiPlanResult::InvalidArgument;
        }
        for (std::size_t index = 0; index < entityKeys.size(); ++index)
        {
            if (!entityKeys[index].IsValid() || (index > 0 && !(entityKeys[index - 1] < entityKeys[index])))
            {
                return WorldAoiPlanResult::InvalidInput;
            }
        }

        try
        {
            // visibleSet 을 조회하여 제거 대상 수집
            std::vector<WorldAoiPrunedVisibility> pruned;
            for (const VisibleSetMap::value_type& entry : visibleSets_)
            {
                for (const WorldEntityKey entityKey : entityKeys)
                {
                    if (std::binary_search(entry.second.begin(), entry.second.end(), entityKey))
                    {
                        // session key, entity key
                        pruned.push_back(WorldAoiPrunedVisibility{entry.first, entityKey});
                    }
                }
            }
            std::sort(pruned.begin(), pruned.end(), PrunedVisibilityLess);

            for (VisibleSetMap::value_type& entry : visibleSets_)
            {
                std::vector<WorldEntityKey>& visible = entry.second; // visible 로 처리해야 하는 기존 entity 목록

                // 해당 목록에서 제거 목록(entityKeys)에 포함된 entityKey 는 제거하여 visible set 갱신
                visible.erase(
                    std::remove_if(visible.begin(), visible.end(), [entityKeys](const WorldEntityKey entityKey) noexcept
                                   { return std::binary_search(entityKeys.begin(), entityKeys.end(), entityKey); }),
                    visible.end());
            }

            *outPrunedVisibilities = std::move(pruned);
            return WorldAoiPlanResult::Planned;
        }
        catch (const std::bad_alloc&)
        {
            return WorldAoiPlanResult::AllocationFailed;
        }
    }

    bool WorldAoiPlanner::RemoveSession(const WorldSessionKey sessionKey) noexcept
    {
        return visibleSets_.erase(sessionKey) != 0;
    }

    std::span<const WorldEntityKey> WorldAoiPlanner::VisibleEntities(const WorldSessionKey sessionKey) const noexcept
    {
        const VisibleSetMap::const_iterator iterator = visibleSets_.find(sessionKey);
        return iterator == visibleSets_.end() ? std::span<const WorldEntityKey>{}
                                              : std::span<const WorldEntityKey>{iterator->second};
    }

    std::size_t WorldAoiPlanner::RecipientCount() const noexcept
    {
        return visibleSets_.size();
    }
} // namespace psnr::world
