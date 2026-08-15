#pragma once

#include "WorldSessionRegistry.h"
#include "WorldSpatialIndex.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace psnr::world
{
    struct WorldAoiRecipient final // AOI 결과를 받아야 하는 접속자 한 명 정보
    {
        WorldSessionKey sessionKey{};         // 대상 플레이어 session key
        WorldEntityKey controlledEntityKey{}; // session key 에 매핑되는 entity key

        [[nodiscard]] friend bool operator==(const WorldAoiRecipient& left,
                                             const WorldAoiRecipient& right) noexcept = default;
    };

    // recipient 한 명에 대해 이전 tick 의 visible set 과 현재 query 결과의 차이 저장
    struct WorldAoiVisibilityDiff final
    {
        WorldSessionKey sessionKey{};
        std::vector<WorldEntityKey> entered; // 이전 tick에는 안 보임, 현재는 보이는 entity
        std::vector<WorldEntityKey> stayed;  // 이전 tick에도 보였고, 현재도 보임
        std::vector<WorldEntityKey> left;    // 이전 tick에는 보였는데, 현재는 안 보임

        [[nodiscard]] friend bool operator==(const WorldAoiVisibilityDiff& left,
                                             const WorldAoiVisibilityDiff& right) noexcept = default;
    };

    // 제거된 entity 결과
    struct WorldAoiPrunedVisibility final
    {
        WorldSessionKey sessionKey{};
        WorldEntityKey entityKey{};

        [[nodiscard]] friend bool operator==(const WorldAoiPrunedVisibility& left,
                                             const WorldAoiPrunedVisibility& right) noexcept = default;
    };

    enum class WorldAoiPlanResult : std::uint8_t // PlanAndCommit 실행 결과
    {
        Planned = 0,
        InvalidArgument,
        InvalidInput,
        AllocationFailed,
    };

    class WorldAoiPlanner final
    {
    public:
        [[nodiscard]] WorldAoiPlanResult PlanAndCommit(std::span<const WorldAoiRecipient> recipients,
                                                       const WorldSpatialIndex& spatialIndex,
                                                       std::vector<WorldAoiVisibilityDiff>* outDiffs) noexcept;

        [[nodiscard]] WorldAoiPlanResult PruneVisibleEntities(
            std::span<const WorldEntityKey> entityKeys,
            std::vector<WorldAoiPrunedVisibility>* outPrunedVisibilities) noexcept;

        bool RemoveSession(WorldSessionKey sessionKey) noexcept;

        [[nodiscard]] std::span<const WorldEntityKey> VisibleEntities(WorldSessionKey sessionKey) const noexcept;
        [[nodiscard]] std::size_t RecipientCount() const noexcept;

    private:
        // world session key 에서 visible 상태로 처리해야 하는 다른 entity key
        using VisibleSetMap = std::unordered_map<WorldSessionKey, std::vector<WorldEntityKey>, WorldSessionKeyHash>;

        VisibleSetMap visibleSets_; // recipient 별 이전 tick의 visible set 소유
    };
} // namespace psnr::world
