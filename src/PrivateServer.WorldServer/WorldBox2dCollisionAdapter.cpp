#include "pch.h"

#include "WorldBox2dCollisionAdapter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace psnr::world
{
    namespace
    {
        constexpr std::uint64_t PlayerCollisionCategoryBits = B2_DEFAULT_CATEGORY_BITS;
        constexpr std::uint64_t PlayerSpawnReservationCategoryBits = 1ull << 1;
        constexpr std::uint64_t PlayerSpawnPlacementMaskBits =
            PlayerCollisionCategoryBits | PlayerSpawnReservationCategoryBits;

        struct CandidateQueryContext final
        {
            std::vector<std::uint64_t>* candidateUserData = nullptr;
        };

        // b2DynamicTree_Query callback function
        // callback( nodeId = proxyId, node->userData = userData, context = rawContext);
        // AABB 가 겹친다면, 후보군에 head 와 충돌한 proxy Id 추가
        [[nodiscard]] bool CollectCandidate(const int proxyId, const std::uint64_t userData,
                                            void* const rawContext) noexcept
        {
            static_cast<void>(proxyId);
            CandidateQueryContext* const context = static_cast<CandidateQueryContext*>(rawContext);
            context->candidateUserData->push_back(userData);
            return true;
        }

        struct PlayerSpawnPlacementQueryContext final
        {
            bool blocked = false;
        };

        // spawn 가능한 자리이면 호출되는 callback
        [[nodiscard]] bool BlockPlayerSpawnPlacement(const int proxyId, const std::uint64_t userData,
                                                     void* const rawContext) noexcept
        {
            static_cast<void>(proxyId);
            static_cast<void>(userData);
            PlayerSpawnPlacementQueryContext* const context =
                static_cast<PlayerSpawnPlacementQueryContext*>(rawContext);
            context->blocked = true;
            return false;
        }

        [[nodiscard]] bool IsValid(const WorldCollisionProxy& proxy) noexcept
        {
            const bool knownRole =
                proxy.role == WorldCollisionProxyRole::PlayerHead || proxy.role == WorldCollisionProxyRole::PlayerBody;
            const bool finiteGeometry = std::isfinite(proxy.startX) && std::isfinite(proxy.startY) &&
                                        std::isfinite(proxy.endX) && std::isfinite(proxy.endY) &&
                                        std::isfinite(proxy.radius);
            const bool validHead =
                proxy.role != WorldCollisionProxyRole::PlayerHead ||
                (proxy.segmentOrdinal == 0 && proxy.startX == proxy.endX && proxy.startY == proxy.endY);
            return proxy.ownerKey.IsValid() && knownRole && finiteGeometry && proxy.radius > 0.0f && validHead;
        }

        [[nodiscard]] b2Circle MakeCircle(const WorldCollisionProxy& proxy) noexcept
        {
            return b2Circle{
                b2Vec2{proxy.startX, proxy.startY},
                proxy.radius,
            };
        }

        [[nodiscard]] b2Capsule MakeCapsule(const WorldCollisionProxy& proxy) noexcept
        {
            return b2Capsule{
                b2Vec2{proxy.startX, proxy.startY},
                b2Vec2{proxy.endX, proxy.endY},
                proxy.radius,
            };
        }

        // AABB 계산하여 반환
        [[nodiscard]] b2AABB ComputeAabb(const WorldCollisionProxy& proxy) noexcept
        {
            // head
            if (proxy.role == WorldCollisionProxyRole::PlayerHead)
            {
                const b2Circle circle = MakeCircle(proxy);
                return b2ComputeCircleAABB(&circle, b2Transform_identity);
            }

            // body
            const b2Capsule capsule = MakeCapsule(proxy);
            return b2ComputeCapsuleAABB(&capsule, b2Transform_identity);
        }

        [[nodiscard]] b2AABB MakeAabb(const WorldPlayerSpawnBounds& bounds) noexcept
        {
            return b2AABB{
                b2Vec2{bounds.minX, bounds.minY},
                b2Vec2{bounds.maxX, bounds.maxY},
            };
        }

        // 실제 충돌 확인
        [[nodiscard]] bool HasExactContact(const WorldCollisionProxy& head,
                                           const WorldCollisionProxy& candidate) noexcept
        {
            const b2Circle headCircle = MakeCircle(head);
            b2Manifold manifold{};
            if (candidate.role == WorldCollisionProxyRole::PlayerHead ||
                (candidate.startX == candidate.endX && candidate.startY == candidate.endY))
            {
                const b2Circle candidateCircle = MakeCircle(candidate);
                manifold = b2CollideCircles(&candidateCircle, b2Transform_identity, &headCircle, b2Transform_identity);
            }
            else
            {
                const b2Capsule candidateCapsule = MakeCapsule(candidate);
                manifold = b2CollideCapsuleAndCircle(&candidateCapsule, b2Transform_identity, &headCircle,
                                                     b2Transform_identity);
            }

            for (int pointIndex = 0; pointIndex < manifold.pointCount; ++pointIndex)
            {
                if (manifold.points[pointIndex].separation <= 0.0f)
                {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] WorldCollisionProxyKey MakeKey(const WorldCollisionProxy& proxy) noexcept
        {
            return WorldCollisionProxyKey{
                proxy.ownerKey,
                proxy.role,
                proxy.segmentOrdinal,
            };
        }

        [[nodiscard]] bool ProxyKeyLess(const WorldCollisionProxyKey& left,
                                        const WorldCollisionProxyKey& right) noexcept
        {
            if (left.ownerKey != right.ownerKey)
            {
                return left.ownerKey < right.ownerKey;
            }
            if (left.role != right.role)
            {
                return left.role < right.role;
            }
            return left.segmentOrdinal < right.segmentOrdinal;
        }

        // key 오름차순으로 contact 생성
        [[nodiscard]] WorldCollisionContact NormalizeContact(const WorldCollisionProxy& first,
                                                             const WorldCollisionProxy& second) noexcept
        {
            const WorldCollisionProxyKey firstKey = MakeKey(first);
            const WorldCollisionProxyKey secondKey = MakeKey(second);
            return ProxyKeyLess(secondKey, firstKey) ? WorldCollisionContact{secondKey, firstKey}
                                                     : WorldCollisionContact{firstKey, secondKey};
        }

        [[nodiscard]] bool ContactLess(const WorldCollisionContact& left, const WorldCollisionContact& right) noexcept
        {
            if (left.first != right.first)
            {
                return ProxyKeyLess(left.first, right.first);
            }
            return ProxyKeyLess(left.second, right.second);
        }
    } // namespace

    WorldBox2dCollisionAdapter::WorldBox2dCollisionAdapter()
        : tree_(b2DynamicTree_Create())
    {
    }

    WorldBox2dCollisionAdapter::~WorldBox2dCollisionAdapter() noexcept
    {
        b2DynamicTree_Destroy(&tree_);
    }

    bool WorldBox2dCollisionAdapter::ProxyKeyLess::operator()(const WorldCollisionProxyKey& left,
                                                              const WorldCollisionProxyKey& right) const noexcept
    {
        return psnr::world::ProxyKeyLess(left, right);
    }

    WorldCollisionQueryResult WorldBox2dCollisionAdapter::Query(
        const std::span<const WorldCollisionProxy> proxies,
        std::vector<WorldCollisionContact>* const outContacts) noexcept
    {
        if (outContacts == nullptr)
        {
            return WorldCollisionQueryResult::InvalidArgument;
        }
        for (const WorldCollisionProxy& proxy : proxies)
        {
            if (!IsValid(proxy))
            {
                return WorldCollisionQueryResult::InvalidInput;
            }
        }

        try
        {
            // persistent tree 동기화 진행
            const WorldCollisionQueryResult synchronizeResult = Synchronize(proxies);
            if (synchronizeResult != WorldCollisionQueryResult::Queried)
            {
                return synchronizeResult;
            }

            candidateUserData_.reserve(treeProxies_.size());

            std::vector<WorldCollisionContact> contacts;
            contacts.reserve(treeProxies_.size());

            // head와 다른 body/head 의 AABB overlapped 탐색
            for (const TreeProxyMap::value_type& pair : treeProxies_)
            {
                const WorldCollisionProxy& head = pair.second.proxy;
                if (head.role != WorldCollisionProxyRole::PlayerHead)
                {
                    continue;
                }

                candidateUserData_.clear();
                CandidateQueryContext queryContext{&candidateUserData_};
                const b2AABB headAabb = ComputeAabb(head); // head AABB

                // Dynamic AABB Tree 질의
                // head AABB 와 겹치는 AABB 를 가진 node의 proxy Id 를 candidate 에 추가
                const b2TreeStats queryStats =
                    b2DynamicTree_Query(&tree_, headAabb, PlayerCollisionCategoryBits, CollectCandidate, &queryContext);
                static_cast<void>(queryStats);

                // AABB 겹치는 후보군 대상 실제 충돌 여부 확인
                for (const std::uint64_t candidateUserData : candidateUserData_)
                {
                    const std::uintptr_t candidateAddress = static_cast<std::uintptr_t>(candidateUserData);
                    const TreeProxyEntry* const candidateEntry =
                        reinterpret_cast<const TreeProxyEntry*>(candidateAddress);
                    const WorldCollisionProxy& candidate = candidateEntry->proxy;
                    if (candidate.ownerKey == head.ownerKey || !HasExactContact(head, candidate))
                    {
                        continue;
                    }
                    contacts.push_back(NormalizeContact(head, candidate));
                }
            }

            std::sort(contacts.begin(), contacts.end(), ContactLess);
            contacts.erase(std::unique(contacts.begin(), contacts.end()), contacts.end());
            *outContacts = std::move(contacts);
            return WorldCollisionQueryResult::Queried;
        }
        catch (...)
        {
            return WorldCollisionQueryResult::AllocationFailed;
        }
    }

    WorldPlayerSpawnReservationResult WorldBox2dCollisionAdapter::TryReservePlayerSpawnPlacement(
        const WorldPlayerSpawnBounds& bounds) noexcept
    {
        if (!WorldPlayerSpawnBounds::IsValid(bounds))
        {
            return WorldPlayerSpawnReservationResult::InvalidInput;
        }

        // 비어있는 공간 query (collision, spawn mask 전부 포함)
        PlayerSpawnPlacementQueryContext queryContext;
        const b2TreeStats queryStats = b2DynamicTree_Query(&tree_, MakeAabb(bounds), PlayerSpawnPlacementMaskBits,
                                                           BlockPlayerSpawnPlacement, &queryContext);
        static_cast<void>(queryStats);
        if (queryContext.blocked) // AABB 겹치는 영역 존재
        {
            return WorldPlayerSpawnReservationResult::Blocked;
        }

        try
        {
            playerSpawnReservationProxyIds_.reserve(playerSpawnReservationProxyIds_.size() + 1);
        }
        catch (...)
        {
            return WorldPlayerSpawnReservationResult::AllocationFailed;
        }

        // 비어있는 경우 노드 추가(spawn mask 전용)
        const int treeProxyId =
            b2DynamicTree_CreateProxy(&tree_, MakeAabb(bounds), PlayerSpawnReservationCategoryBits, 0);
        playerSpawnReservationProxyIds_.push_back(treeProxyId);
        return WorldPlayerSpawnReservationResult::Reserved;
    }

    WorldCollisionQueryResult WorldBox2dCollisionAdapter::Synchronize(
        const std::span<const WorldCollisionProxy> proxies)
    {
        ClearPlayerSpawnReservations();

        if (syncGeneration_ == std::numeric_limits<std::uint64_t>::max())
        {
            for (TreeProxyMap::value_type& pair : treeProxies_)
            {
                pair.second.lastSeenGeneration = 0;
            }
            syncGeneration_ = 1;
        }
        else
        {
            ++syncGeneration_;
        }

        for (const WorldCollisionProxy& proxy : proxies)
        {
            const WorldCollisionProxyKey key = MakeKey(proxy);
            // first WorldCollisionProxyKey : second TreeProxyEntry
            TreeProxyMap::iterator iterator = treeProxies_.find(key);

            if (iterator == treeProxies_.end()) // 새로 추가되는 경우
            {
                // treeProxies 에 신규 entry 로 등록
                // proxyId 는 DynamicTree 에 노드를 추가할 때 부여되는 node index 사용
                const std::pair<TreeProxyMap::iterator, bool> insertion =
                    treeProxies_.emplace(key, TreeProxyEntry{proxy, -1, syncGeneration_});
                TreeProxyEntry& entry = insertion.first->second;

                // DynamicTree 에 신규 등록
                const std::uintptr_t entryAddress = reinterpret_cast<std::uintptr_t>(&entry);
                entry.treeProxyId = b2DynamicTree_CreateProxy(&tree_, ComputeAabb(proxy), PlayerCollisionCategoryBits,
                                                              static_cast<std::uint64_t>(entryAddress));
                continue;
            }

            // treeProxies 에 존재하는 경우
            TreeProxyEntry& entry = iterator->second;
            if (entry.lastSeenGeneration == syncGeneration_)
            {
                return WorldCollisionQueryResult::InvalidInput;
            }
            if (entry.proxy != proxy) // proxy 정보 갱신된 경우 move 처리
            {
                b2DynamicTree_MoveProxy(&tree_, entry.treeProxyId, ComputeAabb(proxy));
                entry.proxy = proxy;
            }
            entry.lastSeenGeneration = syncGeneration_; // sync generation 갱신
        }

        TreeProxyMap::iterator iterator = treeProxies_.begin();
        while (iterator != treeProxies_.end())
        {
            if (iterator->second.lastSeenGeneration == syncGeneration_)
            {
                ++iterator;
                continue;
            }

            // treeProxies 에서 이번에 syncGeneration 이 갱신되지 않은 proxy 는 destroy
            b2DynamicTree_DestroyProxy(&tree_, iterator->second.treeProxyId);
            iterator = treeProxies_.erase(iterator);
        }

        return WorldCollisionQueryResult::Queried;
    }

    void WorldBox2dCollisionAdapter::ClearPlayerSpawnReservations() noexcept
    {
        for (const int treeProxyId : playerSpawnReservationProxyIds_)
        {
            b2DynamicTree_DestroyProxy(&tree_, treeProxyId); // spawn 용 임시 노드는 제거
        }
        playerSpawnReservationProxyIds_.clear();
    }
} // namespace psnr::world
