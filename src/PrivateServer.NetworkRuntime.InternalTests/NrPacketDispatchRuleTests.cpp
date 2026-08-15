#include "pch.h"

#include "NrPacketDispatchRule.h"

#include <type_traits>

namespace psnr::core
{
    namespace
    {
        constexpr NrPacketType HeartbeatPacketType{0};

        TEST(NrPacketDispatchRuleTests, RuleIsSimpleCopyableValue)
        {
            EXPECT_TRUE(std::is_copy_constructible_v<NrPacketDispatchRule>);
            EXPECT_TRUE(std::is_copy_assignable_v<NrPacketDispatchRule>);
            EXPECT_TRUE(std::is_move_constructible_v<NrPacketDispatchRule>);
            EXPECT_TRUE(std::is_move_assignable_v<NrPacketDispatchRule>);
        }

        TEST(NrPacketDispatchRuleTests, DefaultRuleUsesSentinelValues)
        {
            const NrPacketDispatchRule rule;

            EXPECT_EQ(rule.packetType, NrPacketType{});
            EXPECT_EQ(rule.dispatchLane, NrDispatchLane::Count);
        }

        TEST(NrPacketDispatchRuleTests, RuleStoresPacketTypeAndDispatchLane)
        {
            const NrPacketDispatchRule rule{HeartbeatPacketType, NrDispatchLane::ServerIngress};

            EXPECT_EQ(rule.packetType, HeartbeatPacketType);
            EXPECT_EQ(rule.dispatchLane, NrDispatchLane::ServerIngress);
        }
    } // namespace
} // namespace psnr::core
