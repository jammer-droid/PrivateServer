#include "pch.h"

#include "WorldEntityIdentity.h"

#include <cstdint>
#include <type_traits>

namespace psnr::world::tests
{
    static_assert(sizeof(WorldEntityKey) == sizeof(std::uint32_t) * 2);
    static_assert(sizeof(EntityHandle) == sizeof(std::uint32_t) * 2);

    // trivially copyable 은 사용자 정의 복사 로직을 실행하지 않고,
    // 객체를 구성하는 바이트만 복사해도 되는 타입(memcpy 를 해도 별도의 사용자 정의 복사 로직이 필요없는 경우)
    static_assert(std::is_trivially_copyable_v<WorldEntityKey>); // 단순 메모리 복사 값인지 확인
    static_assert(std::is_trivially_copyable_v<EntityHandle>);   // 단순 메모리 복사 값인지 확인

    TEST(WorldEntityIdentityTests, WorldEntityKeyRequiresNonZeroIdAndGeneration)
    {
        constexpr WorldEntityKey Invalid{};
        constexpr WorldEntityKey MissingId{0, 1};
        constexpr WorldEntityKey MissingGeneration{1, 0};
        constexpr WorldEntityKey Valid{1, 1};

        EXPECT_FALSE(Invalid.IsValid());
        EXPECT_FALSE(MissingId.IsValid());
        EXPECT_FALSE(MissingGeneration.IsValid());
        EXPECT_TRUE(Valid.IsValid());
    }

    TEST(WorldEntityIdentityTests, WorldEntityKeyOrdersByEntityIdThenGeneration)
    {
        constexpr WorldEntityKey FirstLifetime{1, 1};
        constexpr WorldEntityKey NextLifetime{1, 2};
        constexpr WorldEntityKey NextEntity{2, 1};

        EXPECT_LT(FirstLifetime, NextLifetime);
        EXPECT_LT(NextLifetime, NextEntity);
    }

    TEST(WorldEntityIdentityTests, EntityHandleAllowsSlotIndexZeroAndRejectsGenerationZero)
    {
        constexpr EntityHandle Invalid{7, 0};
        constexpr EntityHandle FirstSlot{0, 1};
        constexpr EntityHandle OldLifetime{7, 3};
        constexpr EntityHandle NewLifetime{7, 4};

        EXPECT_FALSE(Invalid.IsValid());
        EXPECT_TRUE(FirstSlot.IsValid());
        EXPECT_NE(OldLifetime, NewLifetime);
    }
} // namespace psnr::world::tests
