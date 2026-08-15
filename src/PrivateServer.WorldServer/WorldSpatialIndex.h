#pragma once

#include "WorldSpatialProxy.h"
#include "WorldResult.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace psnr::world
{
    // Config 기반 빈 Index 생성 결과
    // 이번 tick 의 spatial Proxy 목록으로 grid build 결과
    enum class WorldSpatialIndexBuildResult : std::uint8_t
    {
        Built = 0,
        InvalidInput,
        AllocationFailed,
    };

    // observer 주변의 visible entry 검색
    enum class WorldSpatialQueryResult : std::uint8_t
    {
        Queried = 0,
        InvalidArgument,
        ObserverNotFound,
        AllocationFailed,
    };

    class WorldSpatialIndex final
    {
    public:
        ~WorldSpatialIndex();

        WorldSpatialIndex(const WorldSpatialIndex&) = delete;
        WorldSpatialIndex& operator=(const WorldSpatialIndex&) = delete;

        [[nodiscard]] static WorldResult<std::unique_ptr<WorldSpatialIndex>> Create(
            const WorldSpatialConfig& config) noexcept;

        [[nodiscard]] WorldSpatialIndexBuildResult Rebuild(std::span<const WorldSpatialProxy> proxies) noexcept;
        [[nodiscard]] WorldSpatialIndexBuildResult Rebuild(std::vector<WorldSpatialProxy>&& proxies) noexcept;

        [[nodiscard]] WorldSpatialQueryResult QueryVisibilityBands(
            WorldEntityKey observerEntityKey, std::vector<WorldEntityKey>* outEnteredRadiusEntityKeys,
            std::vector<WorldEntityKey>* outRetainedRadiusEntityKeys) const noexcept;

        [[nodiscard]] const WorldSpatialConfig& Config() const noexcept;
        [[nodiscard]] std::size_t ProxyCount() const noexcept;
        [[nodiscard]] std::size_t OccupiedCellCount() const noexcept;

    private:
        class Impl;

        WorldSpatialIndex(const WorldSpatialConfig& config, std::unique_ptr<Impl> impl) noexcept;
        [[nodiscard]] WorldSpatialIndexBuildResult RebuildOwned(std::vector<WorldSpatialProxy>&& proxies) noexcept;

        WorldSpatialConfig config_{};
        std::unique_ptr<Impl> impl_;
    };
} // namespace psnr::world
