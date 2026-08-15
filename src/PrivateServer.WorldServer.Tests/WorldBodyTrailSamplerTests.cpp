#include "pch.h"

#include "WorldBodyTrailSampler.h"

#include <limits>
#include <vector>

namespace psnr::world::tests
{
    namespace
    {
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

        [[nodiscard]] BodyTrailComponent MakeBodyTrail(const std::uint32_t maxSampleCount, const BodyTrailSample sample)
        {
            WorldResult<BodyTrailComponent> result = CreateBodyTrailComponent(maxSampleCount);
            EXPECT_TRUE(result.Succeeded());
            BodyTrailComponent bodyTrail = result.TakeValue();
            EXPECT_TRUE(bodyTrail.TryPushBack(sample));
            return bodyTrail;
        }
    } // namespace

    TEST(WorldBodyTrailSamplerTests, RecordsDueTicksInHeadToTailOrder)
    {
        constexpr WorldBodyTrailSampleConfig Config{3, 4};
        BodyTrailComponent bodyTrail;

        EXPECT_EQ(WorldBodyTrailSampler::Sample(Config, 1, 1.0f, 2.0f, bodyTrail), WorldBodyTrailSampleResult::NotDue);
        EXPECT_TRUE(bodyTrail.Empty());

        ASSERT_EQ(WorldBodyTrailSampler::Sample(Config, 3, 3.0f, 4.0f, bodyTrail), WorldBodyTrailSampleResult::Sampled);
        ASSERT_EQ(WorldBodyTrailSampler::Sample(Config, 6, 6.0f, 7.0f, bodyTrail), WorldBodyTrailSampleResult::Sampled);

        const std::vector<BodyTrailSample> expected{
            BodyTrailSample{6.0f, 7.0f},
            BodyTrailSample{3.0f, 4.0f},
        };
        EXPECT_EQ(ReadSamples(bodyTrail), expected);
    }

    TEST(WorldBodyTrailSamplerTests, RejectsInvalidInputWithoutChangingTrail)
    {
        const BodyTrailComponent Unchanged = MakeBodyTrail(4, BodyTrailSample{1.0f, 2.0f});
        BodyTrailComponent bodyTrail = Unchanged;
        constexpr WorldBodyTrailSampleConfig InvalidConfig{};

        EXPECT_EQ(WorldBodyTrailSampler::Sample(InvalidConfig, 3, 3.0f, 4.0f, bodyTrail),
                  WorldBodyTrailSampleResult::InvalidConfig);
        EXPECT_EQ(bodyTrail, Unchanged);
        EXPECT_EQ(WorldBodyTrailSampler::Sample(WorldBodyTrailSampleConfig{3, 4}, 3,
                                                std::numeric_limits<float>::infinity(), 4.0f, bodyTrail),
                  WorldBodyTrailSampleResult::InvalidPosition);
        EXPECT_EQ(bodyTrail, Unchanged);
    }

    TEST(WorldBodyTrailSamplerTests, RejectsCapacityOverflowWithoutOverwritingTail)
    {
        constexpr WorldBodyTrailSampleConfig Config{1, 1};
        BodyTrailComponent bodyTrail;
        ASSERT_EQ(WorldBodyTrailSampler::Sample(Config, 1, 1.0f, 0.0f, bodyTrail), WorldBodyTrailSampleResult::Sampled);
        const BodyTrailComponent unchanged = bodyTrail;

        EXPECT_EQ(WorldBodyTrailSampler::Sample(Config, 2, 2.0f, 0.0f, bodyTrail),
                  WorldBodyTrailSampleResult::CapacityExceeded);
        EXPECT_EQ(bodyTrail, unchanged);
    }
} // namespace psnr::world::tests
