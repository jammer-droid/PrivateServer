#include "pch.h"

#include "WorldSpatialIndex.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <new>
#include <unordered_map>
#include <utility>
#include <vector>

namespace psnr::world
{
    namespace
    {
        struct SpatialCellKey final
        {
            std::int64_t x = 0;
            std::int64_t y = 0;

            [[nodiscard]] friend bool operator==(const SpatialCellKey& left,
                                                 const SpatialCellKey& right) noexcept = default;
        };

        struct SpatialCellKeyHash final
        {
            [[nodiscard]] std::size_t operator()(const SpatialCellKey& key) const noexcept
            {
                const std::uint64_t x = static_cast<std::uint64_t>(key.x);
                const std::uint64_t y = static_cast<std::uint64_t>(key.y);
                const std::uint64_t mixed = x ^ (y + 0x9e3779b97f4a7c15ull + (x << 6) + (x >> 2));
                return static_cast<std::size_t>(mixed);
            }
        };

        [[nodiscard]] bool TryCellCoordinate(const float worldCoordinate, const float cellSize,
                                             std::int64_t* const outCoordinate) noexcept
        {
            const double scaled = std::floor(static_cast<double>(worldCoordinate) / static_cast<double>(cellSize));
            if (!std::isfinite(scaled) || scaled < static_cast<double>((std::numeric_limits<std::int64_t>::min)()) ||
                scaled > static_cast<double>((std::numeric_limits<std::int64_t>::max)()))
            {
                return false;
            }

            *outCoordinate = static_cast<std::int64_t>(scaled);
            return true;
        }

        [[nodiscard]] bool TryCellRange(const float centerX, const float centerY, const float radius,
                                        const float cellSize, SpatialCellKey* const outMinimum,
                                        SpatialCellKey* const outMaximum) noexcept
        {
            return TryCellCoordinate(centerX - radius, cellSize, &outMinimum->x) &&
                   TryCellCoordinate(centerY - radius, cellSize, &outMinimum->y) &&
                   TryCellCoordinate(centerX + radius, cellSize, &outMaximum->x) &&
                   TryCellCoordinate(centerY + radius, cellSize, &outMaximum->y);
        }

        [[nodiscard]] bool ExactVisibilityMatches(const WorldSpatialProxy& observer, const WorldSpatialProxy& target,
                                                  const float interestRadius) noexcept
        {
            const double differenceX = static_cast<double>(observer.centerX) - static_cast<double>(target.centerX);
            const double differenceY = static_cast<double>(observer.centerY) - static_cast<double>(target.centerY);
            const double maximumDistance =
                static_cast<double>(interestRadius) + static_cast<double>(target.circleRadius);
            return differenceX * differenceX + differenceY * differenceY <= maximumDistance * maximumDistance;
        }
    } // namespace

    class WorldSpatialIndex::Impl final // rebuild 할 때마다 생성
    {
    public:
        using CellEntries = std::vector<std::size_t>;
        using CellMap = std::unordered_map<SpatialCellKey, CellEntries, SpatialCellKeyHash>;
        using EntityIndexMap = std::unordered_map<WorldEntityKey, std::size_t, WorldEntityKeyHash>;

        std::vector<WorldSpatialProxy> proxies;
        CellMap cells;                // cellKey - cellEntries
        EntityIndexMap entityIndexes; // entityKey - proxies[proxyIndex]
    };

    bool IsValid(const WorldSpatialConfig& config) noexcept
    {
        return std::isfinite(config.spatialCellSize) && config.spatialCellSize > 0.0f &&
               std::isfinite(config.aoiEnterRadius) && config.aoiEnterRadius > 0.0f &&
               std::isfinite(config.aoiRetainRadius) && config.aoiEnterRadius < config.aoiRetainRadius;
    }

    bool IsValid(const WorldSpatialProxy& proxy) noexcept
    {
        const bool validKind = proxy.entityKind == WorldEntityKind::Player ||
                               proxy.entityKind == WorldEntityKind::Resource ||
                               proxy.entityKind == WorldEntityKind::StaticObstacle;
        return proxy.entityKey.IsValid() && validKind && std::isfinite(proxy.centerX) && std::isfinite(proxy.centerY) &&
               std::isfinite(proxy.circleRadius) && proxy.circleRadius > 0.0f;
    }

    WorldResult<std::unique_ptr<WorldSpatialIndex>> WorldSpatialIndex::Create(const WorldSpatialConfig& config) noexcept
    {
        if (!IsValid(config))
        {
            return WorldResult<std::unique_ptr<WorldSpatialIndex>>::Failure(WorldErrorCode::InvalidConfig);
        }

        try
        {
            std::unique_ptr<Impl> impl = std::make_unique<Impl>();
            std::unique_ptr<WorldSpatialIndex> index{
                new WorldSpatialIndex(config, std::move(impl)),
            };
            return WorldResult<std::unique_ptr<WorldSpatialIndex>>{std::move(index)};
        }
        catch (...)
        {
            return WorldResult<std::unique_ptr<WorldSpatialIndex>>::Failure(WorldErrorCode::AllocationFailed);
        }
    }

    WorldSpatialIndexBuildResult WorldSpatialIndex::Rebuild(const std::span<const WorldSpatialProxy> proxies) noexcept
    {
        try
        {
            std::vector<WorldSpatialProxy> ownedProxies{proxies.begin(), proxies.end()};
            return RebuildOwned(std::move(ownedProxies));
        }
        catch (...)
        {
            return WorldSpatialIndexBuildResult::AllocationFailed;
        }
    }

    WorldSpatialIndexBuildResult WorldSpatialIndex::Rebuild(std::vector<WorldSpatialProxy>&& proxies) noexcept
    {
        return RebuildOwned(std::move(proxies));
    }

    WorldSpatialIndexBuildResult WorldSpatialIndex::RebuildOwned(std::vector<WorldSpatialProxy>&& proxies) noexcept
    {
        try
        {
            std::unique_ptr<Impl> rebuilt = std::make_unique<Impl>();
            rebuilt->proxies = std::move(proxies);
            std::sort(rebuilt->proxies.begin(), rebuilt->proxies.end(), WorldSpatialProxyEntityKeyLess{});
            rebuilt->entityIndexes.reserve(rebuilt->proxies.size());

            for (std::size_t proxyIndex = 0; proxyIndex < rebuilt->proxies.size(); ++proxyIndex)
            {
                const WorldSpatialProxy& proxy = rebuilt->proxies[proxyIndex];
                if (!IsValid(proxy))
                {
                    return WorldSpatialIndexBuildResult::InvalidInput;
                }

                const std::pair<Impl::EntityIndexMap::iterator, bool> inserted =
                    rebuilt->entityIndexes.emplace(proxy.entityKey, proxyIndex);
                if (!inserted.second)
                {
                    return WorldSpatialIndexBuildResult::InvalidInput;
                }

                SpatialCellKey minimumCell;
                SpatialCellKey maximumCell;
                if (!TryCellRange(proxy.centerX, proxy.centerY, proxy.circleRadius, config_.spatialCellSize,
                                  &minimumCell, &maximumCell))
                {
                    return WorldSpatialIndexBuildResult::InvalidInput;
                }

                for (std::int64_t cellY = minimumCell.y;; ++cellY)
                {
                    for (std::int64_t cellX = minimumCell.x;; ++cellX)
                    {
                        rebuilt->cells[SpatialCellKey{cellX, cellY}].push_back(proxyIndex);
                        if (cellX == maximumCell.x)
                        {
                            break;
                        }
                    }

                    if (cellY == maximumCell.y)
                    {
                        break;
                    }
                }
            }

            impl_ = std::move(rebuilt);
            return WorldSpatialIndexBuildResult::Built;
        }
        catch (...)
        {
            return WorldSpatialIndexBuildResult::AllocationFailed;
        }
    }

    WorldSpatialQueryResult WorldSpatialIndex::QueryVisibilityBands(
        const WorldEntityKey observerEntityKey, std::vector<WorldEntityKey>* const outEnteredRadiusEntityKeys,
        std::vector<WorldEntityKey>* const outRetainedRadiusEntityKeys) const noexcept
    {
        if (outEnteredRadiusEntityKeys == nullptr || outRetainedRadiusEntityKeys == nullptr ||
            outEnteredRadiusEntityKeys == outRetainedRadiusEntityKeys || !observerEntityKey.IsValid())
        {
            return WorldSpatialQueryResult::InvalidArgument;
        }

        const Impl::EntityIndexMap::const_iterator observerIterator = impl_->entityIndexes.find(observerEntityKey);
        if (observerIterator == impl_->entityIndexes.end())
        {
            return WorldSpatialQueryResult::ObserverNotFound;
        }

        try
        {
            const WorldSpatialProxy& observer = impl_->proxies[observerIterator->second];
            SpatialCellKey minimumCell;
            SpatialCellKey maximumCell;
            if (!TryCellRange(observer.centerX, observer.centerY, config_.aoiRetainRadius, config_.spatialCellSize,
                              &minimumCell, &maximumCell))
            {
                return WorldSpatialQueryResult::InvalidArgument;
            }

            std::vector<std::size_t> candidateIndexes;
            for (std::int64_t cellY = minimumCell.y;; ++cellY)
            {
                for (std::int64_t cellX = minimumCell.x;; ++cellX)
                {
                    const Impl::CellMap::const_iterator cellIterator = impl_->cells.find(SpatialCellKey{cellX, cellY});
                    if (cellIterator != impl_->cells.end())
                    {
                        candidateIndexes.insert(candidateIndexes.end(), cellIterator->second.begin(),
                                                cellIterator->second.end());
                    }

                    if (cellX == maximumCell.x)
                    {
                        break;
                    }
                }

                if (cellY == maximumCell.y)
                {
                    break;
                }
            }

            std::sort(candidateIndexes.begin(), candidateIndexes.end());
            candidateIndexes.erase(std::unique(candidateIndexes.begin(), candidateIndexes.end()),
                                   candidateIndexes.end());

            std::vector<WorldEntityKey> enteredRadiusEntityKeys;
            std::vector<WorldEntityKey> retainedRadiusEntityKeys;
            enteredRadiusEntityKeys.reserve(candidateIndexes.size());
            retainedRadiusEntityKeys.reserve(candidateIndexes.size());
            for (const std::size_t candidateIndex : candidateIndexes)
            {
                const WorldSpatialProxy& target = impl_->proxies[candidateIndex];
                if (target.entityKey == observerEntityKey)
                {
                    continue;
                }

                if (ExactVisibilityMatches(observer, target, config_.aoiRetainRadius))
                {
                    retainedRadiusEntityKeys.push_back(target.entityKey);
                    if (ExactVisibilityMatches(observer, target, config_.aoiEnterRadius))
                    {
                        enteredRadiusEntityKeys.push_back(target.entityKey);
                    }
                }
            }

            *outEnteredRadiusEntityKeys = std::move(enteredRadiusEntityKeys);
            *outRetainedRadiusEntityKeys = std::move(retainedRadiusEntityKeys);
            return WorldSpatialQueryResult::Queried;
        }
        catch (...)
        {
            return WorldSpatialQueryResult::AllocationFailed;
        }
    }

    const WorldSpatialConfig& WorldSpatialIndex::Config() const noexcept
    {
        return config_;
    }

    std::size_t WorldSpatialIndex::ProxyCount() const noexcept
    {
        return impl_->proxies.size();
    }

    std::size_t WorldSpatialIndex::OccupiedCellCount() const noexcept
    {
        return impl_->cells.size();
    }

    WorldSpatialIndex::~WorldSpatialIndex() = default;

    WorldSpatialIndex::WorldSpatialIndex(const WorldSpatialConfig& config, std::unique_ptr<Impl> impl) noexcept
        : config_(config)
        , impl_(std::move(impl))
    {
    }
} // namespace psnr::world
