#include "pch.h"

#include "WorldPhysicsScene.h"

#include "WorldBox2dCollisionAdapter.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <new>
#include <utility>
#include <vector>

namespace psnr::world
{
    namespace
    {
        struct Vector2 final
        {
            float x = 0.0f;
            float y = 0.0f;
        };

        struct SweepHit final
        {
            bool hit = false;
            float timeOfImpact = 0.0f;

            Vector2 normal{};
            PhysicsColliderKey collider{};
        };

        [[nodiscard]] float Dot(const Vector2 left, const Vector2 right) noexcept
        {
            return left.x * right.x + left.y * right.y;
        }

        [[nodiscard]] float LengthSquared(const Vector2 value) noexcept
        {
            return Dot(value, value);
        }

        [[nodiscard]] Vector2 NormalizeOrZero(const Vector2 value) noexcept
        {
            const float lengthSquared = LengthSquared(value);
            if (lengthSquared <= 0.0f)
            {
                return Vector2{};
            }

            const float inverseLength = 1.0f / std::sqrt(lengthSquared);
            return Vector2{value.x * inverseLength, value.y * inverseLength};
        }

        [[nodiscard]] Vector2 ProxyCenter(const WorldPhysicsProxyProjection& projection) noexcept
        {
            return Vector2{
                projection.ownerPositionX + projection.proxy.localOffsetX,
                projection.ownerPositionY + projection.proxy.localOffsetY,
            };
        }

        // left.mask 가 right.layer 를 허용하는지
        // right.mask 가 left.layer 를 허용하는지
        [[nodiscard]] bool Interacts(const PhysicsProxy& left, const PhysicsProxy& right) noexcept
        {
            return (left.mask & right.layer) != 0 && (right.mask & left.layer) != 0;
        }

        // startCenter, startCenter + displacement 의 AABB 와 staticCenter 의 sweep 확인
        [[nodiscard]] bool SweptAabbOverlapsCircle(const Vector2 startCenter, const Vector2 displacement,
                                                   const float movingRadius, const Vector2 staticCenter,
                                                   const float staticRadius) noexcept
        {
            const Vector2 endCenter{startCenter.x + displacement.x, startCenter.y + displacement.y};
            const float movingMinimumX = (std::min)(startCenter.x, endCenter.x) - movingRadius;
            const float movingMaximumX = (std::max)(startCenter.x, endCenter.x) + movingRadius;
            const float movingMinimumY = (std::min)(startCenter.y, endCenter.y) - movingRadius;
            const float movingMaximumY = (std::max)(startCenter.y, endCenter.y) + movingRadius;
            const float staticMinimumX = staticCenter.x - staticRadius;
            const float staticMaximumX = staticCenter.x + staticRadius;
            const float staticMinimumY = staticCenter.y - staticRadius;
            const float staticMaximumY = staticCenter.y + staticRadius;

            return movingMinimumX <= staticMaximumX && movingMaximumX >= staticMinimumX &&
                   movingMinimumY <= staticMaximumY && movingMaximumY >= staticMinimumY;
        }

        [[nodiscard]] bool PhysicsProxyKeyLess(const PhysicsProxyKey& left, const PhysicsProxyKey& right) noexcept
        {
            if (left.ownerKey != right.ownerKey)
            {
                return left.ownerKey < right.ownerKey;
            }

            return left.fixtureId.value < right.fixtureId.value;
        }

        // 동률 후보 중에 가장 작은 ColliderKey 선택
        // timeOfImpact 비교가 아니라 명시적인 identity 순서 사용
        // arena -> left, bottom, right, top 순서
        // entityProxy -> ownerKey 우선, owner가 같으면 fixtureId value 우선
        [[nodiscard]] bool ColliderKeyLess(const PhysicsColliderKey& left, const PhysicsColliderKey& right) noexcept
        {
            if (left.kind != right.kind)
            {
                return static_cast<std::uint8_t>(left.kind) < static_cast<std::uint8_t>(right.kind);
            }

            if (left.kind == PhysicsColliderKind::ArenaBoundary)
            {
                return static_cast<std::uint8_t>(left.arenaBoundary) < static_cast<std::uint8_t>(right.arenaBoundary);
            }

            return PhysicsProxyKeyLess(left.entityProxy, right.entityProxy);
        }

        void RecordHit(const float timeOfImpact, const Vector2 normal, const PhysicsColliderKey& collider,
                       std::vector<SweepHit>* const outHits)
        {
            outHits->push_back(SweepHit{true, timeOfImpact, normal, collider});
        }

        [[nodiscard]] SweepHit SelectStableHit(const std::span<const SweepHit> candidates,
                                               const float contactTolerance) noexcept
        {
            if (candidates.empty())
            {
                return SweepHit{};
            }

            float minimumTimeOfImpact = candidates.front().timeOfImpact;
            for (const SweepHit& candidate : candidates) // 최소 timeOfImpact 찾기
            {
                minimumTimeOfImpact = (std::min)(minimumTimeOfImpact, candidate.timeOfImpact);
            }

            // contactTolerance 범위까지 후보 확장
            const float tiedMaximumTimeOfImpact = minimumTimeOfImpact + contactTolerance;
            const SweepHit* selected = nullptr;
            for (const SweepHit& candidate : candidates)
            {
                if (candidate.timeOfImpact > tiedMaximumTimeOfImpact)
                {
                    continue;
                }

                if (selected == nullptr || ColliderKeyLess(candidate.collider, selected->collider))
                {
                    selected = &candidate;
                }
            }

            return selected == nullptr ? SweepHit{} : *selected;
        }

        void SweepArenaBoundary(const float displacementComponent, const float startComponent,
                                const float allowedComponent, const Vector2 normal, const PhysicsArenaBoundary boundary,
                                std::vector<SweepHit>* const outHits)
        {
            if (displacementComponent == 0.0f)
            {
                return;
            }

            // allowedComponent 와 startComponent 가 충돌한 경우, 전체 이동량의 몇 % 지점인지 계산
            const float timeOfImpact = (allowedComponent - startComponent) / displacementComponent;
            if (timeOfImpact < 0.0f || timeOfImpact > 1.0f) // 이번 이동에는 충돌을 하지 않음
            {
                return;
            }

            RecordHit(timeOfImpact, normal,
                      PhysicsColliderKey{PhysicsColliderKind::ArenaBoundary, boundary, PhysicsProxyKey{}}, outHits);
        }

        // arena 를 sweep 하며 충돌 확인
        void SweepArena(const Vector2 startCenter, const Vector2 displacement, const float radius,
                        const WorldPhysicsArenaBounds& arenaBounds, std::vector<SweepHit>* const outHits)
        {
            if (displacement.x < 0.0f)
            {
                SweepArenaBoundary(displacement.x, startCenter.x, arenaBounds.minimumX + radius, Vector2{1.0f, 0.0f},
                                   PhysicsArenaBoundary::Left, outHits);
            }
            else if (displacement.x > 0.0f)
            {
                SweepArenaBoundary(displacement.x, startCenter.x, arenaBounds.maximumX - radius, Vector2{-1.0f, 0.0f},
                                   PhysicsArenaBoundary::Right, outHits);
            }

            if (displacement.y < 0.0f)
            {
                SweepArenaBoundary(displacement.y, startCenter.y, arenaBounds.minimumY + radius, Vector2{0.0f, 1.0f},
                                   PhysicsArenaBoundary::Bottom, outHits);
            }
            else if (displacement.y > 0.0f)
            {
                SweepArenaBoundary(displacement.y, startCenter.y, arenaBounds.maximumY - radius, Vector2{0.0f, -1.0f},
                                   PhysicsArenaBoundary::Top, outHits);
            }
        }

        // 초기 침투 확인
        [[nodiscard]] bool HasInitialArenaPenetration(const Vector2 center, const float radius,
                                                      const WorldPhysicsArenaBounds& arenaBounds,
                                                      const float contactTolerance) noexcept
        {
            return center.x - radius < arenaBounds.minimumX - contactTolerance ||
                   center.x + radius > arenaBounds.maximumX + contactTolerance ||
                   center.y - radius < arenaBounds.minimumY - contactTolerance ||
                   center.y + radius > arenaBounds.maximumY + contactTolerance;
        }

        [[nodiscard]] bool HasInitialCirclePenetration(const Vector2 movingCenter, const float movingRadius,
                                                       const Vector2 staticCenter, const float staticRadius,
                                                       const float contactTolerance) noexcept
        {
            const Vector2 difference{movingCenter.x - staticCenter.x, movingCenter.y - staticCenter.y};
            const float minimumDistance = movingRadius + staticRadius - contactTolerance;
            if (minimumDistance <= 0.0f)
            {
                return false;
            }

            return LengthSquared(difference) < minimumDistance * minimumDistance;
        }

        // 두 원의 충돌 = 중심 사이의 거리가 반지름 합과 같아지는 순간
        // startCenter에서 displacement 속도로 움직이는 원과 static center 가 충돌하는 지점 t 를 찾아야 함
        // displacement = velocity × fixedDt = 이번 tick의 전체 이동량
        // P(t) = startPos + displacement * t = 이번 tick 에서 전체 이동량 중에서 t 비율만큼 이동했을 때의 위치
        // dist(P(t), static.pos) = combined 되는 순간을 찾으면 t 비율만큼 이동한 순간에 첫 충돌 확인 가능
        void SweepStaticCircle(const Vector2 startCenter, const Vector2 displacement, const float movingRadius,
                               const WorldPhysicsProxyProjection& staticProjection,
                               std::vector<SweepHit>* const outHits)
        {
            const Vector2 staticCenter = ProxyCenter(staticProjection);
            const float combinedRadius = movingRadius + staticProjection.proxy.shape.radius; // start 에 radius 몰아주기

            // staticCenter가 (0, 0) 인 것처럼 계산
            const Vector2 relativeStart{startCenter.x - staticCenter.x, startCenter.y - staticCenter.y};

            // r = relativeStart
            // d = displacement
            // |r + dt|^2 = combinedRadius^2
            // |d|^2 t^2 + 2(r·d)t + |r|^2 - combinedRadius^2 = 0
            const float a = LengthSquared(displacement);
            const float b = Dot(relativeStart, displacement);
            const float c = LengthSquared(relativeStart) - combinedRadius * combinedRadius;

            // a 는 이동량(displacement) 길이 제곱 -> 음수가 될 수 없음. 0이면 이동 없음
            // b 는 시작 상대 위치와 이동 방향의 내적 -> b < 0 일 때만 서로 충돌 가능성이 있음
            if (a <= 0.0f || b >= 0.0f)
            {
                return;
            }

            // 판별식으로 이차방정식 실수 해 확인
            // - 실수 해가 있으면 두 원이 충돌하는 시점 존재
            const float discriminant = b * b - a * c;
            if (discriminant < 0.0f) // 해가 없음
            {
                return;
            }

            // timeOfImpact 는 전체 displacement 중 얼마나 이동했는지 나타내는 비율
            // displacement 는 이번 tick의 전체 이동량
            // timeOfImpact = 0 -> displacement 0% 적용
            // timeOfImpact = 0.5 -> displacement 50% 작용
            // timeOfImpact = 1.0 -> displacement 100% 적용
            const float timeOfImpact = (-b - std::sqrt(discriminant)) / a; // 첫 접촉 면만 확인
            if (timeOfImpact < 0.0f || timeOfImpact > 1.0f)                // 충돌 지점이 displacement 의 범위 바깥
            {
                return;
            }

            const Vector2 hitCenter{
                startCenter.x + displacement.x * timeOfImpact,
                startCenter.y + displacement.y * timeOfImpact,
            };
            const Vector2 normal = NormalizeOrZero(Vector2{hitCenter.x - staticCenter.x, hitCenter.y - staticCenter.y});
            if (LengthSquared(normal) <= 0.0f)
            {
                return;
            }

            RecordHit(timeOfImpact, normal,
                      PhysicsColliderKey{
                          PhysicsColliderKind::EntityProxy,
                          PhysicsArenaBoundary::Invalid,
                          PhysicsProxyKey{staticProjection.proxy.ownerKey, staticProjection.proxy.fixtureId},
                      },
                      outHits);
        }

        [[nodiscard]] bool CirclesOverlap(const Vector2 leftCenter, const float leftRadius, const Vector2 rightCenter,
                                          const float rightRadius) noexcept
        {
            const Vector2 difference{leftCenter.x - rightCenter.x, leftCenter.y - rightCenter.y};
            const float combinedRadius = leftRadius + rightRadius;
            return LengthSquared(difference) <= combinedRadius * combinedRadius;
        }

        [[nodiscard]] WorldTriggerOverlap NormalizeTriggerOverlap(const PhysicsProxyKey& left,
                                                                  const PhysicsProxyKey& right) noexcept
        {
            if (PhysicsProxyKeyLess(right, left))
            {
                return WorldTriggerOverlap{right, left};
            }

            return WorldTriggerOverlap{left, right};
        }

        [[nodiscard]] bool TriggerOverlapLess(const WorldTriggerOverlap& left,
                                              const WorldTriggerOverlap& right) noexcept
        {
            if (left.first != right.first)
            {
                return PhysicsProxyKeyLess(left.first, right.first);
            }

            return PhysicsProxyKeyLess(left.second, right.second);
        }

        [[nodiscard]] bool PhysicsContactLess(const WorldPhysicsContact& left,
                                              const WorldPhysicsContact& right) noexcept
        {
            if (left.movingOwnerKey != right.movingOwnerKey)
            {
                return left.movingOwnerKey < right.movingOwnerKey;
            }

            if (left.contactOrdinal != right.contactOrdinal)
            {
                return left.contactOrdinal < right.contactOrdinal;
            }

            return ColliderKeyLess(left.collider, right.collider);
        }

        [[nodiscard]] WorldResult<void> ValidateStepInput(
            const std::span<const WorldPhysicsMovementInput> movementInputs,
            const std::span<const WorldPhysicsProxyProjection> staticSolidProxies,
            const std::span<const WorldPhysicsProxyProjection> triggerProxies,
            const WorldPhysicsArenaBounds& arenaBounds, const float fixedDeltaSeconds) noexcept
        {
            if (!WorldPhysicsArenaBounds::IsValid(arenaBounds) || !std::isfinite(fixedDeltaSeconds) ||
                fixedDeltaSeconds <= 0.0f)
            {
                return WorldResult<void>::Failure(WorldErrorCode::InvalidInput);
            }

            for (const WorldPhysicsMovementInput& movementInput : movementInputs)
            {
                if (!IsValid(movementInput) || movementInput.movingProxy.proxy.behavior != PhysicsProxyBehavior::Solid)
                {
                    return WorldResult<void>::Failure(WorldErrorCode::InvalidInput);
                }
            }

            for (const WorldPhysicsProxyProjection& staticProxy : staticSolidProxies)
            {
                if (!IsValid(staticProxy) || staticProxy.proxy.behavior != PhysicsProxyBehavior::Solid)
                {
                    return WorldResult<void>::Failure(WorldErrorCode::InvalidInput);
                }
            }

            for (const WorldPhysicsProxyProjection& triggerProxy : triggerProxies)
            {
                if (!IsValid(triggerProxy) || triggerProxy.proxy.behavior != PhysicsProxyBehavior::Trigger)
                {
                    return WorldResult<void>::Failure(WorldErrorCode::InvalidInput);
                }
            }

            return WorldResult<void>::Success();
        }
    } // namespace

    bool IsValid(const PhysicsProxy& proxy) noexcept
    {
        const bool validBehavior =
            proxy.behavior == PhysicsProxyBehavior::Solid || proxy.behavior == PhysicsProxyBehavior::Trigger;

        return proxy.ownerKey.IsValid() && proxy.ownerHandle.IsValid() && proxy.fixtureId.IsValid() &&
               std::isfinite(proxy.localOffsetX) && std::isfinite(proxy.localOffsetY) &&
               std::isfinite(proxy.shape.radius) && proxy.shape.radius > 0.0f && proxy.layer != 0 && validBehavior;
    }

    bool IsValid(const WorldPhysicsConfig& config) noexcept
    {
        return config.maxContactsPerTick == WorldPhysicsMaxContactsPerTick && std::isfinite(config.collisionEpsilon) &&
               config.collisionEpsilon > 0.0f && std::isfinite(config.contactTolerance) &&
               config.contactTolerance > 0.0f;
    }

    bool WorldPhysicsArenaBounds::IsValid(const WorldPhysicsArenaBounds& arenaBounds) noexcept
    {
        return std::isfinite(arenaBounds.minimumX) && std::isfinite(arenaBounds.minimumY) &&
               std::isfinite(arenaBounds.maximumX) && std::isfinite(arenaBounds.maximumY) &&
               arenaBounds.minimumX < arenaBounds.maximumX && arenaBounds.minimumY < arenaBounds.maximumY;
    }

    bool IsValid(const WorldPhysicsProxyProjection& projection) noexcept
    {
        return IsValid(projection.proxy) && std::isfinite(projection.ownerPositionX) &&
               std::isfinite(projection.ownerPositionY);
    }

    bool IsValid(const WorldPhysicsMovementInput& input) noexcept
    {
        return IsValid(input.movingProxy) && std::isfinite(input.movementInputX) &&
               std::isfinite(input.movementInputY) && std::isfinite(input.maxMoveSpeed) && input.maxMoveSpeed >= 0.0f;
    }

    WorldResult<std::unique_ptr<WorldPhysicsScene>> WorldPhysicsScene::Create(const WorldPhysicsConfig& config) noexcept
    {
        if (!IsValid(config))
        {
            return WorldResult<std::unique_ptr<WorldPhysicsScene>>::Failure(WorldErrorCode::InvalidConfig);
        }

        try
        {
            std::unique_ptr<WorldPhysicsScene> scene{new WorldPhysicsScene(config)};
            return WorldResult<std::unique_ptr<WorldPhysicsScene>>{std::move(scene)};
        }
        catch (const std::bad_alloc&)
        {
            return WorldResult<std::unique_ptr<WorldPhysicsScene>>::Failure(WorldErrorCode::AllocationFailed);
        }
        catch (...)
        {
            return WorldResult<std::unique_ptr<WorldPhysicsScene>>::Failure(WorldErrorCode::AllocationFailed);
        }
    }

    WorldPhysicsScene::~WorldPhysicsScene() noexcept = default;

    WorldResult<WorldPhysicsStepResult> WorldPhysicsScene::Compute(
        const std::span<const WorldPhysicsMovementInput> movementInputs,       // 물리 처리 입력
        const std::span<const WorldPhysicsProxyProjection> staticSolidProxies, // solid objects
        const std::span<const WorldPhysicsProxyProjection> triggerProxies,     // trigger objects
        const WorldPhysicsArenaBounds& arenaBounds,                            // 맵 경계
        const float fixedDeltaSeconds) const noexcept                          // fixedDt
    {
        const WorldResult<void> validationResult =
            ValidateStepInput(movementInputs, staticSolidProxies, triggerProxies, arenaBounds, fixedDeltaSeconds);
        if (validationResult.Failed())
        {
            return WorldResult<WorldPhysicsStepResult>::Failure(validationResult.Error());
        }

        try
        {
            std::vector<WorldResolvedMotion> resolvedMotions; // 물리 처리 결과
            std::vector<WorldPhysicsContact> contacts;        // contact 기록
            std::vector<WorldTriggerOverlap> triggerOverlaps;
            std::vector<SweepHit> hitCandidates;
            resolvedMotions.reserve(movementInputs.size());
            contacts.reserve(movementInputs.size() * config_.maxContactsPerTick);
            triggerOverlaps.reserve(movementInputs.size() * triggerProxies.size());
            hitCandidates.reserve(staticSolidProxies.size() + 2);

            for (const WorldPhysicsMovementInput& movementInput : movementInputs)
            {
                const WorldPhysicsProxyProjection& movingProjection = movementInput.movingProxy;
                const PhysicsProxy& movingProxy = movingProjection.proxy;
                const float movingRadius = movingProxy.shape.radius;
                const Vector2 initialCenter = ProxyCenter(movingProjection); // 실제 center (ownerPos + offset)

                if (HasInitialArenaPenetration(initialCenter, movingRadius, arenaBounds, config_.contactTolerance))
                {
                    // 초기값으로 침투 영역 발생하면 return
                    return WorldResult<WorldPhysicsStepResult>::Failure(WorldErrorCode::InitialPenetration);
                }

                for (const WorldPhysicsProxyProjection& staticProxy : staticSolidProxies) // 장애물 충돌 확인
                {
                    if (!Interacts(movingProxy, staticProxy.proxy))
                    {
                        continue;
                    }

                    const Vector2 staticCenter = ProxyCenter(staticProxy); // 장애물의 center
                    if (HasInitialCirclePenetration(initialCenter, movingRadius, staticCenter,
                                                    staticProxy.proxy.shape.radius, config_.contactTolerance))
                    {
                        return WorldResult<WorldPhysicsStepResult>::Failure(WorldErrorCode::InitialPenetration);
                    }
                }

                Vector2 movementDirection{movementInput.movementInputX, movementInput.movementInputY};
                if (LengthSquared(movementDirection) > 1.0f) // 입력의 크기가 1.0을 넘으면 정규화
                {
                    movementDirection = NormalizeOrZero(movementDirection);
                }

                const Vector2 desiredVelocity{
                    movementDirection.x * movementInput.maxMoveSpeed,
                    movementDirection.y * movementInput.maxMoveSpeed,
                };

                // 이번 tick에서 얼마나 이동할 것인가
                // 충돌이 있으면 값이 조절될 수 있음
                Vector2 remainingDisplacement{
                    desiredVelocity.x * fixedDeltaSeconds,
                    desiredVelocity.y * fixedDeltaSeconds,
                };
                Vector2 currentCenter = initialCenter;
                bool contactCapReached = false;

                for (std::uint32_t contactOrdinal = 0; contactOrdinal < config_.maxContactsPerTick; ++contactOrdinal)
                {
                    if (LengthSquared(remainingDisplacement) <= config_.collisionEpsilon * config_.collisionEpsilon)
                    {
                        remainingDisplacement = Vector2{}; // 매우 작은 이동량은 0.0 으로
                        break;
                    }

                    hitCandidates.clear();
                    SweepArena(currentCenter, remainingDisplacement, movingRadius, arenaBounds,
                               &hitCandidates); // arena sweep

                    for (const WorldPhysicsProxyProjection& staticProxy : staticSolidProxies)
                    {
                        if (!Interacts(movingProxy, staticProxy.proxy))
                        {
                            continue;
                        }

                        const Vector2 staticCenter = ProxyCenter(staticProxy);
                        if (!SweptAabbOverlapsCircle(currentCenter, remainingDisplacement, movingRadius, staticCenter,
                                                     staticProxy.proxy.shape.radius))
                        {
                            continue;
                        }

                        SweepStaticCircle(currentCenter, remainingDisplacement, movingRadius, staticProxy,
                                          &hitCandidates);
                    }

                    // ordinal 하나에서 발견한 모든 후보를 hitCandidates 에 저장
                    const SweepHit earliestHit = SelectStableHit(hitCandidates, config_.contactTolerance);
                    if (!earliestHit.hit)
                    {
                        currentCenter.x += remainingDisplacement.x;
                        currentCenter.y += remainingDisplacement.y;
                        remainingDisplacement = Vector2{};
                        break;
                    }

                    currentCenter.x += remainingDisplacement.x * earliestHit.timeOfImpact;
                    currentCenter.y += remainingDisplacement.y * earliestHit.timeOfImpact;
                    contacts.push_back(WorldPhysicsContact{
                        movingProxy.ownerKey,
                        contactOrdinal,
                        earliestHit.collider,
                        earliestHit.timeOfImpact,
                        earliestHit.normal.x,
                        earliestHit.normal.y,
                    });

                    const float remainingFraction = 1.0f - earliestHit.timeOfImpact;
                    remainingDisplacement.x *= remainingFraction;
                    remainingDisplacement.y *= remainingFraction;
                    const float inwardDistance = Dot(remainingDisplacement, earliestHit.normal);
                    if (inwardDistance < 0.0f) // normal 반대 방향, static 안쪽으로 이동함
                    {
                        // 남은 displacement 에서 static 안쪽으로 향하는 성분 제거
                        remainingDisplacement.x -= earliestHit.normal.x * inwardDistance;
                        remainingDisplacement.y -= earliestHit.normal.y * inwardDistance;
                    }
                }

                // 최대 충돌 처리 사용 후에도 처리하지 못한 이동량이 있나 확인
                if (LengthSquared(remainingDisplacement) > config_.collisionEpsilon * config_.collisionEpsilon)
                {
                    contactCapReached = true;
                }

                const float ownerPositionX = currentCenter.x - movingProxy.localOffsetX;
                const float ownerPositionY = currentCenter.y - movingProxy.localOffsetY;
                const float resolvedVelocityX =
                    contactCapReached ? 0.0f : (ownerPositionX - movingProjection.ownerPositionX) / fixedDeltaSeconds;
                const float resolvedVelocityY =
                    contactCapReached ? 0.0f : (ownerPositionY - movingProjection.ownerPositionY) / fixedDeltaSeconds;

                resolvedMotions.push_back(WorldResolvedMotion{
                    movingProxy.ownerKey,
                    movingProxy.ownerHandle,
                    ownerPositionX,
                    ownerPositionY,
                    resolvedVelocityX,
                    resolvedVelocityY,
                    contactCapReached,
                });

                // 최종 physics 처리 이후 trigger 충돌쌍 검출
                const PhysicsProxyKey movingKey{movingProxy.ownerKey, movingProxy.fixtureId};
                for (const WorldPhysicsProxyProjection& triggerProjection : triggerProxies)
                {
                    if (!Interacts(movingProxy, triggerProjection.proxy))
                    {
                        continue;
                    }

                    const Vector2 triggerCenter = ProxyCenter(triggerProjection);
                    if (!CirclesOverlap(currentCenter, movingRadius, triggerCenter,
                                        triggerProjection.proxy.shape.radius))
                    {
                        continue;
                    }

                    const PhysicsProxyKey triggerKey{triggerProjection.proxy.ownerKey,
                                                     triggerProjection.proxy.fixtureId};
                    triggerOverlaps.push_back(NormalizeTriggerOverlap(movingKey, triggerKey));
                }
            }

            std::sort(contacts.begin(), contacts.end(), PhysicsContactLess);
            std::sort(triggerOverlaps.begin(), triggerOverlaps.end(), TriggerOverlapLess);
            triggerOverlaps.erase(std::unique(triggerOverlaps.begin(), triggerOverlaps.end()), triggerOverlaps.end());

            return WorldResult<WorldPhysicsStepResult>(WorldPhysicsStepResult{
                std::move(resolvedMotions),
                std::move(contacts),
                std::move(triggerOverlaps),
            });
        }
        catch (const std::bad_alloc&)
        {
            return WorldResult<WorldPhysicsStepResult>::Failure(WorldErrorCode::AllocationFailed);
        }
        catch (...)
        {
            return WorldResult<WorldPhysicsStepResult>::Failure(WorldErrorCode::OperationFailed);
        }
    }

    const WorldPhysicsConfig& WorldPhysicsScene::Config() const noexcept
    {
        return config_;
    }

    WorldResult<std::vector<WorldCollisionContact>> WorldPhysicsScene::QueryPlayerCollisions(
        const std::span<const WorldCollisionProxy> proxies) noexcept
    {
        std::vector<WorldCollisionContact> contacts;
        const WorldCollisionQueryResult queryResult = collisionAdapter_->Query(proxies, &contacts);
        if (queryResult == WorldCollisionQueryResult::Queried)
        {
            return WorldResult<std::vector<WorldCollisionContact>>(std::move(contacts));
        }
        if (queryResult == WorldCollisionQueryResult::InvalidArgument)
        {
            return WorldResult<std::vector<WorldCollisionContact>>::Failure(WorldErrorCode::InvalidArgument);
        }
        if (queryResult == WorldCollisionQueryResult::InvalidInput)
        {
            return WorldResult<std::vector<WorldCollisionContact>>::Failure(WorldErrorCode::InvalidInput);
        }
        if (queryResult == WorldCollisionQueryResult::AllocationFailed)
        {
            return WorldResult<std::vector<WorldCollisionContact>>::Failure(WorldErrorCode::AllocationFailed);
        }
        return WorldResult<std::vector<WorldCollisionContact>>::Failure(WorldErrorCode::OperationFailed);
    }

    WorldPlayerSpawnReservationResult WorldPhysicsScene::TryReservePlayerSpawnPlacement(
        const WorldPlayerSpawnBounds& bounds) noexcept
    {
        return collisionAdapter_->TryReservePlayerSpawnPlacement(bounds);
    }

    WorldPhysicsScene::WorldPhysicsScene(const WorldPhysicsConfig& config)
        : config_(config)
        , collisionAdapter_(std::make_unique<WorldBox2dCollisionAdapter>())
    {
    }
} // namespace psnr::world
