#include "pch.h"

#include "NrDispatchLane.h"
#include "NrPacketType.h"
#include "NrTypeTraits.h"

#include <cstdint>
#include <type_traits>

namespace psnr::core
{

    namespace
    {
        struct MoveOnlyHandle
        {
            MoveOnlyHandle() = default;
            MoveOnlyHandle(const MoveOnlyHandle&) = delete;
            MoveOnlyHandle& operator=(const MoveOnlyHandle&) = delete;
            MoveOnlyHandle(MoveOnlyHandle&&) noexcept = default;
            MoveOnlyHandle& operator=(MoveOnlyHandle&&) noexcept = default;
            ~MoveOnlyHandle() noexcept = default;

            void Reset() noexcept {}

            [[nodiscard]] bool IsValid() const noexcept
            {
                return true;
            }
        };

        struct CopyableHandle
        {
            void Reset() noexcept {}

            [[nodiscard]] bool IsValid() const noexcept
            {
                return true;
            }
        };

        struct MissingReset
        {
            MissingReset() = default;
            MissingReset(const MissingReset&) = delete;
            MissingReset& operator=(const MissingReset&) = delete;
            MissingReset(MissingReset&&) noexcept = default;
            MissingReset& operator=(MissingReset&&) noexcept = default;
            ~MissingReset() noexcept = default;

            [[nodiscard]] bool IsValid() const noexcept
            {
                return true;
            }
        };

        struct MissingIsValid
        {
            MissingIsValid() = default;
            MissingIsValid(const MissingIsValid&) = delete;
            MissingIsValid& operator=(const MissingIsValid&) = delete;
            MissingIsValid(MissingIsValid&&) noexcept = default;
            MissingIsValid& operator=(MissingIsValid&&) noexcept = default;
            ~MissingIsValid() noexcept = default;

            void Reset() noexcept {}
        };

        struct ThrowingDestructor
        {
            ThrowingDestructor() = default;
            ThrowingDestructor(const ThrowingDestructor&) = delete;
            ThrowingDestructor& operator=(const ThrowingDestructor&) = delete;
            ThrowingDestructor(ThrowingDestructor&&) noexcept = default;
            ThrowingDestructor& operator=(ThrowingDestructor&&) noexcept = default;
            ~ThrowingDestructor() noexcept(false) {}

            void Reset() noexcept {}

            [[nodiscard]] bool IsValid() const noexcept
            {
                return true;
            }
        };

        TEST(TypeTraitsTests, DetectsMoveOnlyTypes)
        {
            EXPECT_TRUE(NrConceptMoveOnly<MoveOnlyHandle>);
            EXPECT_FALSE(NrConceptMoveOnly<CopyableHandle>);
        }

        TEST(TypeTraitsTests, DetectsNoThrowDestructors)
        {
            EXPECT_TRUE(NrConceptNoThrowDestructible<MoveOnlyHandle>);
            EXPECT_FALSE(NrConceptNoThrowDestructible<ThrowingDestructor>);
        }

        TEST(TypeTraitsTests, DetectsTriviallyCopyableTypes)
        {
            EXPECT_TRUE(NrConceptTriviallyCopyable<CopyableHandle>);
            EXPECT_FALSE(NrConceptTriviallyCopyable<ThrowingDestructor>);
        }

        TEST(TypeTraitsTests, DetectsResetAndIsValidMembers)
        {
            EXPECT_TRUE(NrConceptResettable<MoveOnlyHandle>);
            EXPECT_TRUE(NrConceptValidityInspectable<MoveOnlyHandle>);
            EXPECT_FALSE(NrConceptResettable<MissingReset>);
            EXPECT_FALSE(NrConceptValidityInspectable<MissingIsValid>);
        }

        TEST(TypeTraitsTests, DetectsMoveOnlyRaiiShape)
        {
            EXPECT_TRUE(NrConceptMoveOnlyRaii<MoveOnlyHandle>);
            EXPECT_FALSE(NrConceptMoveOnlyRaii<CopyableHandle>);
            EXPECT_FALSE(NrConceptMoveOnlyRaii<MissingReset>);
            EXPECT_FALSE(NrConceptMoveOnlyRaii<MissingIsValid>);
            EXPECT_FALSE(NrConceptMoveOnlyRaii<ThrowingDestructor>);
        }

        TEST(TypeTraitsTests, PacketTypeIsFixedWidthNumericStrongValue)
        {
            static_assert(std::is_same_v<decltype(NrPacketType::value), std::uint16_t>);
            static_assert(NrConceptTriviallyCopyable<NrPacketType>);

            constexpr NrPacketType packetType{0x1234};
            EXPECT_EQ(packetType.value, 0x1234u);
        }

        TEST(TypeTraitsTests, DispatchLaneUsesDenseRuntimeValues)
        {
            EXPECT_EQ(static_cast<int>(NrDispatchLane::ServerIngress), 0);
            EXPECT_EQ(static_cast<int>(NrDispatchLane::SessionIngress), 1);
            EXPECT_EQ(static_cast<int>(NrDispatchLane::WorldIngress), 2);
            EXPECT_EQ(static_cast<int>(NrDispatchLane::Count), 3);
        }
    } // namespace
} // namespace psnr::core
