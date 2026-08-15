#pragma once

#include <cstddef>
#include <compare>
#include <cstdint>

namespace psnr::world
{
    // Client replication과 World command correlation에 사용하는 논리 entity identity다.
    // Registry slot이나 인증 token이 아니며 entityId와 generation 모두 0이 아니어야 한다.
    struct WorldEntityKey final
    {
        std::uint32_t entityId = 0;   // 0 == invalid
        std::uint32_t generation = 0; // 0 == invalid

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return entityId != 0 && generation != 0;
        }

        [[nodiscard]] friend constexpr bool operator==(const WorldEntityKey& left,
                                                       const WorldEntityKey& right) noexcept = default;

        // <, <=, >, >= 연산자 오버로딩
        // default 면 구조체 멤버가 선언된 순서로 비교
        // 결과는 std::strong_ordering::less / equal / greater 중 하나
        [[nodiscard]] friend constexpr std::strong_ordering operator<=>(const WorldEntityKey& left,
                                                                        const WorldEntityKey& right) noexcept = default;
    };

    struct WorldEntityKeyHash final
    {
        [[nodiscard]] std::size_t operator()(WorldEntityKey key) const noexcept;
    };

    // WorldEntityRegistry 내부 slot을 가리키는 식별자
    struct EntityHandle final
    {
        std::uint32_t slotIndex = 0;      // 0 부터 slot index 시작, 반환될 slot 위치
        std::uint32_t slotGeneration = 0; // 0 == invalid

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return slotGeneration != 0;
        }

        [[nodiscard]] friend constexpr bool operator==(const EntityHandle& left,
                                                       const EntityHandle& right) noexcept = default;
    };
} // namespace psnr::world
