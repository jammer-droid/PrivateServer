#include "pch.h"

#include "WorldBodyTrailTrimmer.h"

#include <initializer_list>
#include <limits>
#include <vector>

namespace psnr::world::tests
{
    namespace
    {
        [[nodiscard]] BodyTrailComponent MakeBodyTrail(const std::uint32_t maxSampleCount,
                                                       const std::initializer_list<BodyTrailSample> samples)
        {
            WorldResult<BodyTrailComponent> result = CreateBodyTrailComponent(maxSampleCount);
            EXPECT_TRUE(result.Succeeded());
            BodyTrailComponent bodyTrail = result.TakeValue();
            for (const BodyTrailSample sample : samples)
            {
                EXPECT_TRUE(bodyTrail.TryPushBack(sample));
            }
            return bodyTrail;
        }

        [[nodiscard]] std::vector<BodyTrailSample> ReadSamples(const BodyTrailComponent& bodyTrail)
        {
            std::vector<BodyTrailSample> samples;
            samples.reserve(bodyTrail.SampleCount());
            for (std::size_t index = 0; index < bodyTrail.SampleCount(); ++index)
            {
                BodyTrailSample sample;
                EXPECT_TRUE(bodyTrail.TryRead(index, &sample));
                samples.push_back(sample);
            }
            return samples;
        }
    } // namespace

    TEST(WorldBodyTrailTrimmerTests, InterpolatesTailAtNominalLength)
    {
        BodyTrailComponent bodyTrail = MakeBodyTrail(8, {
                                                            BodyTrailSample{3.0f, 0.0f},
                                                            BodyTrailSample{6.0f, 0.0f},
                                                            BodyTrailSample{10.0f, 0.0f},
                                                        });

        ASSERT_EQ(WorldBodyTrailTrimmer::Trim(0.0f, 0.0f, 5.0f, bodyTrail), WorldBodyTrailTrimResult::Trimmed);
        const std::vector<BodyTrailSample> expected{
            BodyTrailSample{3.0f, 0.0f},
            BodyTrailSample{5.0f, 0.0f},
        };
        EXPECT_EQ(ReadSamples(bodyTrail), expected);
    }

    TEST(WorldBodyTrailTrimmerTests, InterpolatesInsideHeadToLatestSampleSegment)
    {
        BodyTrailComponent bodyTrail = MakeBodyTrail(8, {BodyTrailSample{3.0f, 4.0f}});

        ASSERT_EQ(WorldBodyTrailTrimmer::Trim(0.0f, 0.0f, 2.5f, bodyTrail), WorldBodyTrailTrimResult::Trimmed);
        ASSERT_EQ(bodyTrail.SampleCount(), 1u);
        BodyTrailSample tail;
        ASSERT_TRUE(bodyTrail.TryRead(0, &tail));
        EXPECT_FLOAT_EQ(tail.positionX, 1.5f);
        EXPECT_FLOAT_EQ(tail.positionY, 2.0f);
    }

    TEST(WorldBodyTrailTrimmerTests, RemovesOnlySamplesPastExactTailBoundary)
    {
        BodyTrailComponent bodyTrail = MakeBodyTrail(8, {
                                                            BodyTrailSample{3.0f, 0.0f},
                                                            BodyTrailSample{5.0f, 0.0f},
                                                            BodyTrailSample{9.0f, 0.0f},
                                                        });

        ASSERT_EQ(WorldBodyTrailTrimmer::Trim(0.0f, 0.0f, 5.0f, bodyTrail), WorldBodyTrailTrimResult::Trimmed);
        const std::vector<BodyTrailSample> expected{
            BodyTrailSample{3.0f, 0.0f},
            BodyTrailSample{5.0f, 0.0f},
        };
        EXPECT_EQ(ReadSamples(bodyTrail), expected);
    }

    TEST(WorldBodyTrailTrimmerTests, PreservesTrailShorterThanNominalLength)
    {
        const BodyTrailComponent unchanged = MakeBodyTrail(8, {
                                                                  BodyTrailSample{3.0f, 0.0f},
                                                                  BodyTrailSample{5.0f, 0.0f},
                                                              });
        BodyTrailComponent bodyTrail = unchanged;

        EXPECT_EQ(WorldBodyTrailTrimmer::Trim(0.0f, 0.0f, 10.0f, bodyTrail), WorldBodyTrailTrimResult::WithinLength);
        EXPECT_EQ(bodyTrail, unchanged);
    }

    TEST(WorldBodyTrailTrimmerTests, RejectsInvalidInputWithoutChangingTrail)
    {
        const BodyTrailComponent unchanged = MakeBodyTrail(8, {BodyTrailSample{1.0f, 2.0f}});
        BodyTrailComponent bodyTrail = unchanged;

        EXPECT_EQ(WorldBodyTrailTrimmer::Trim(0.0f, 0.0f, 0.0f, bodyTrail), WorldBodyTrailTrimResult::InvalidInput);
        EXPECT_EQ(bodyTrail, unchanged);

        ASSERT_TRUE(bodyTrail.TryPushBack(BodyTrailSample{std::numeric_limits<float>::infinity(), 3.0f}));
        const BodyTrailComponent invalidTrail = bodyTrail;
        EXPECT_EQ(WorldBodyTrailTrimmer::Trim(0.0f, 0.0f, 5.0f, bodyTrail), WorldBodyTrailTrimResult::InvalidInput);
        EXPECT_EQ(bodyTrail, invalidTrail);

        bodyTrail = MakeBodyTrail(8, {BodyTrailSample{std::numeric_limits<float>::max(), 0.0f}});
        const BodyTrailComponent overflowTrail = bodyTrail;
        EXPECT_EQ(WorldBodyTrailTrimmer::Trim(-std::numeric_limits<float>::max(), 0.0f, 5.0f, bodyTrail),
                  WorldBodyTrailTrimResult::InvalidInput);
        EXPECT_EQ(bodyTrail, overflowTrail);
    }
} // namespace psnr::world::tests
