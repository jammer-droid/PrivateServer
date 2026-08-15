#include "pch.h"

#include "WorldPlayerBody.h"

namespace psnr::world::tests
{
    namespace
    {
        constexpr WorldPlayerBodyConfig Config{
            WorldGrowthConfig{10.0f, 0.25f, 1.0f, 0.01f},
            8,
        };

        [[nodiscard]] WorldEntityComponents MakePlayerComponents()
        {
            WorldEntityComponents components;
            components.transform = TransformComponent{5.0f, 7.0f, 0.0f};
            components.replicationMetadata = ReplicationMetadataComponent{
                WorldEntityKind::Player,
                1,
                WorldShapeKind::Circle,
                0.5f,
            };
            components.playerControl = PlayerControlComponent{1};
            return components;
        }
    } // namespace

    TEST(WorldPlayerBodyTests, InitializesHeadAnchorAndStraightTailBehindSpawnHeading)
    {
        WorldEntityComponents components = MakePlayerComponents();

        ASSERT_EQ(WorldPlayerBody::Initialize(Config, &components), WorldPlayerBodyUpdateResult::Updated);
        ASSERT_EQ(components.bodyTrail.SampleCount(), 2u);
        EXPECT_EQ(components.bodyTrail.MaxSampleCount(), 8u);
        EXPECT_FLOAT_EQ(components.replicationMetadata.primaryCircleRadius, 0.5f);

        BodyTrailSample headAnchor;
        BodyTrailSample tail;
        ASSERT_TRUE(components.bodyTrail.TryRead(0, &headAnchor));
        ASSERT_TRUE(components.bodyTrail.TryRead(1, &tail));
        EXPECT_EQ(headAnchor, (BodyTrailSample{5.0f, 7.0f}));
        EXPECT_EQ(tail, (BodyTrailSample{-5.0f, 7.0f}));
    }

    TEST(WorldPlayerBodyTests, FinalizesLengthAndRadiusFromCommittedGrowthPoint)
    {
        WorldEntityComponents components = MakePlayerComponents();
        ASSERT_EQ(WorldPlayerBody::Initialize(Config, &components), WorldPlayerBodyUpdateResult::Updated);
        ASSERT_TRUE(components.bodyTrail.TryWrite(1, BodyTrailSample{-10.0f, 7.0f}));

        ASSERT_EQ(WorldPlayerBody::Finalize(Config, 0, &components), WorldPlayerBodyUpdateResult::Updated);
        BodyTrailSample tail;
        ASSERT_TRUE(components.bodyTrail.TryRead(1, &tail));
        EXPECT_EQ(tail, (BodyTrailSample{-5.0f, 7.0f}));
        EXPECT_FLOAT_EQ(components.replicationMetadata.primaryCircleRadius, 0.5f);

        ASSERT_EQ(WorldPlayerBody::Finalize(Config, 2, &components), WorldPlayerBodyUpdateResult::Updated);
        ASSERT_TRUE(components.bodyTrail.TryRead(1, &tail));
        EXPECT_EQ(tail, (BodyTrailSample{-5.0f, 7.0f}));
        EXPECT_FLOAT_EQ(components.replicationMetadata.primaryCircleRadius, 0.51f);
    }

    TEST(WorldPlayerBodyTests, RejectsInvalidConfigWithoutChangingEntity)
    {
        const WorldEntityComponents unchanged = MakePlayerComponents();
        WorldEntityComponents components = unchanged;
        WorldPlayerBodyConfig invalidConfig = Config;
        invalidConfig.maxTrailSampleCount = 1;

        EXPECT_EQ(WorldPlayerBody::Initialize(invalidConfig, &components), WorldPlayerBodyUpdateResult::InvalidConfig);
        EXPECT_EQ(components, unchanged);
    }
} // namespace psnr::world::tests
