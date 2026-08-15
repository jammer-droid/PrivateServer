#include "pch.h"

#include "NrPacketDispatchTable.h"

#include <array>
#include <span>

namespace psnr::core
{
    namespace
    {
        constexpr NrPacketType HeartbeatPacketType{0};
        constexpr NrPacketType MoveInputPacketType{1};
        constexpr NrPacketType CustomPacketType{999};

        TEST(NrPacketDispatchTableTests, CreateEmptyTableAndFindReturnsDispatchRuleNotFound)
        {
            const std::span<const NrPacketDispatchRule> rules;
            NrResult<NrPacketDispatchTable> tableResult = NrPacketDispatchTable::Create(rules);

            ASSERT_TRUE(tableResult.Succeeded());

            NrPacketDispatchTable table = tableResult.TakeValue();
            NrPacketDispatchRule foundRule;
            const NrStatus findStatus = table.Find(HeartbeatPacketType, foundRule);

            EXPECT_TRUE(findStatus.Failed());
            EXPECT_EQ(findStatus.ErrorCode(), NrErrorCode::DispatchRuleNotFound);
        }

        TEST(NrPacketDispatchTableTests, FindReturnsRegisteredRule)
        {
            const std::array<NrPacketDispatchRule, 1> rules = {
                NrPacketDispatchRule{HeartbeatPacketType, NrDispatchLane::ServerIngress},
            };
            NrResult<NrPacketDispatchTable> tableResult = NrPacketDispatchTable::Create(std::span(rules));

            ASSERT_TRUE(tableResult.Succeeded());

            NrPacketDispatchTable table = tableResult.TakeValue();
            NrPacketDispatchRule foundRule;
            const NrStatus findStatus = table.Find(HeartbeatPacketType, foundRule);

            EXPECT_TRUE(findStatus.Succeeded());
            EXPECT_EQ(foundRule.packetType, HeartbeatPacketType);
            EXPECT_EQ(foundRule.dispatchLane, NrDispatchLane::ServerIngress);
        }

        TEST(NrPacketDispatchTableTests, FindReturnsMoveInputWorldIngressRule)
        {
            const std::array<NrPacketDispatchRule, 2> rules = {
                NrPacketDispatchRule{HeartbeatPacketType, NrDispatchLane::ServerIngress},
                NrPacketDispatchRule{MoveInputPacketType, NrDispatchLane::WorldIngress},
            };
            NrResult<NrPacketDispatchTable> tableResult = NrPacketDispatchTable::Create(std::span(rules));

            ASSERT_TRUE(tableResult.Succeeded());

            NrPacketDispatchTable table = tableResult.TakeValue();
            NrPacketDispatchRule foundRule;
            const NrStatus findStatus = table.Find(MoveInputPacketType, foundRule);

            EXPECT_TRUE(findStatus.Succeeded());
            EXPECT_EQ(foundRule.packetType, MoveInputPacketType);
            EXPECT_EQ(foundRule.dispatchLane, NrDispatchLane::WorldIngress);
        }

        TEST(NrPacketDispatchTableTests, FindUnregisteredKnownPacketTypeReturnsDispatchRuleNotFound)
        {
            const std::array<NrPacketDispatchRule, 1> rules = {
                NrPacketDispatchRule{HeartbeatPacketType, NrDispatchLane::ServerIngress},
            };
            NrResult<NrPacketDispatchTable> tableResult = NrPacketDispatchTable::Create(std::span(rules));

            ASSERT_TRUE(tableResult.Succeeded());

            NrPacketDispatchTable table = tableResult.TakeValue();
            NrPacketDispatchRule foundRule;
            const NrStatus findStatus = table.Find(MoveInputPacketType, foundRule);

            EXPECT_TRUE(findStatus.Failed());
            EXPECT_EQ(findStatus.ErrorCode(), NrErrorCode::DispatchRuleNotFound);
        }

        TEST(NrPacketDispatchTableTests, FindUnknownPacketTypeReturnsDispatchRuleNotFound)
        {
            const std::array<NrPacketDispatchRule, 1> rules = {
                NrPacketDispatchRule{HeartbeatPacketType, NrDispatchLane::ServerIngress},
            };
            NrResult<NrPacketDispatchTable> tableResult = NrPacketDispatchTable::Create(std::span(rules));

            ASSERT_TRUE(tableResult.Succeeded());

            NrPacketDispatchTable table = tableResult.TakeValue();
            NrPacketDispatchRule foundRule;
            const NrStatus findStatus = table.Find(CustomPacketType, foundRule);

            EXPECT_TRUE(findStatus.Failed());
            EXPECT_EQ(findStatus.ErrorCode(), NrErrorCode::DispatchRuleNotFound);
        }

        TEST(NrPacketDispatchTableTests, CreateAcceptsArbitraryNumericPacketType)
        {
            const std::array<NrPacketDispatchRule, 1> rules = {
                NrPacketDispatchRule{CustomPacketType, NrDispatchLane::ServerIngress},
            };

            NrResult<NrPacketDispatchTable> tableResult = NrPacketDispatchTable::Create(std::span(rules));

            ASSERT_TRUE(tableResult.Succeeded());

            NrPacketDispatchTable table = tableResult.TakeValue();
            NrPacketDispatchRule foundRule;
            EXPECT_TRUE(table.Find(CustomPacketType, foundRule).Succeeded());
            EXPECT_EQ(foundRule.packetType, CustomPacketType);
        }

        TEST(NrPacketDispatchTableTests, CreateRejectsUnknownDispatchLane)
        {
            const std::array<NrPacketDispatchRule, 1> rules = {
                NrPacketDispatchRule{HeartbeatPacketType, static_cast<NrDispatchLane>(999)},
            };

            NrResult<NrPacketDispatchTable> tableResult = NrPacketDispatchTable::Create(std::span(rules));

            EXPECT_TRUE(tableResult.Failed());
            EXPECT_EQ(tableResult.ErrorCode(), NrErrorCode::InvalidArgument);
        }

        TEST(NrPacketDispatchTableTests, CreateRejectsDuplicatePacketTypeRules)
        {
            const std::array<NrPacketDispatchRule, 2> rules = {
                NrPacketDispatchRule{HeartbeatPacketType, NrDispatchLane::ServerIngress},
                NrPacketDispatchRule{HeartbeatPacketType, NrDispatchLane::SessionIngress},
            };

            NrResult<NrPacketDispatchTable> tableResult = NrPacketDispatchTable::Create(std::span(rules));

            EXPECT_TRUE(tableResult.Failed());
            EXPECT_EQ(tableResult.ErrorCode(), NrErrorCode::InvalidArgument);
        }
    } // namespace
} // namespace psnr::core
