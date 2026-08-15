#pragma once

#include "WorldEntityIdentity.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace psnr::world
{
    enum class WorldResourceOrigin : std::uint8_t
    {
        Invalid = 0,
        Ambient,   // 일반 맵 생성 리소스
        DeathDrop, // death drop 리소스
    };

    struct WorldResourcePosition final
    {
        float positionX = 0.0f;
        float positionY = 0.0f;

        [[nodiscard]] friend bool operator==(const WorldResourcePosition& left,
                                             const WorldResourcePosition& right) noexcept = default;
    };

    // Resource 정보
    struct WorldResourceInstance final
    {
        WorldEntityKey entityKey{};  // resource entity 식별
        EntityHandle entityHandle{}; // resource entity 식별
        WorldResourceOrigin origin = WorldResourceOrigin::Invalid;
        std::uint32_t ambientSlotId = 0; // 일반 리소스인 경우, 어떤 spawn slot 생성인지
        float positionX = 0.0f;
        float positionY = 0.0f;

        [[nodiscard]] static bool ContainsAmbientSlot(
            std::span<const WorldResourceInstance> resources, std::uint32_t ambientSlotId) noexcept;

        [[nodiscard]] friend bool operator==(const WorldResourceInstance& left,
                                             const WorldResourceInstance& right) noexcept = default;
    };

    enum class WorldResourceRegisterResult : std::uint8_t
    {
        Registered = 0,
        InvalidArgument,
        DuplicateEntity,
        DuplicatePosition,
        AllocationFailed,
    };

    enum class WorldResourceReserveResult : std::uint8_t
    {
        Reserved = 0,
        CapacityExceeded,
        AllocationFailed,
    };

    enum class WorldResourceRemoveResult : std::uint8_t
    {
        Removed = 0,
        InvalidArgument,
        NotFound,
        IdentityMismatch,
        StateInvariantViolation,
    };

    // 현재 월드에 실제로 존재하는 resource instance와 exact-position index를 함께 소유한다.
    // Ambient spawn/respawn 정책은 WorldResourceSlotState가 별도로 소유한다.
    class WorldResourceRegistry final
    {
    public:
        [[nodiscard]] WorldResourceReserveResult ReserveAdditional(std::size_t additionalCount) noexcept;
        [[nodiscard]] WorldResourceRegisterResult TryRegister(const WorldResourceInstance& instance) noexcept;
        [[nodiscard]] WorldResourceRemoveResult TryRemove(WorldEntityKey entityKey, EntityHandle entityHandle) noexcept;
        [[nodiscard]] bool TryFind(WorldEntityKey entityKey, WorldResourceInstance* outInstance) const noexcept;
        [[nodiscard]] bool TryFindAmbientSlot(std::uint32_t ambientSlotId,
                                              WorldResourceInstance* outInstance) const noexcept;
        [[nodiscard]] bool ContainsExactPosition(float positionX, float positionY) const noexcept;

        // WorldEntityKey 오름차순이며 다음 registry mutation 전까지만 유효하다.
        [[nodiscard]] std::span<const WorldResourceInstance> Instances() const noexcept;
        [[nodiscard]] std::size_t Count() const noexcept;

    private:
        std::vector<WorldResourceInstance> resources_;
        std::vector<WorldResourcePosition> positions_;
    };
} // namespace psnr::world
