#include "pch.h"

#include "WorldDeterministicSampler.h"

namespace psnr::world::tests
{
    TEST(WorldDeterministicSamplerTests, ProducesStableDomainSeparatedUnitSamples)
    {
        WorldResult<float> firstPlayerSampleResult =
            WorldDeterministicSampler::SampleUnit(WorldDeterministicSampleDomain::PlayerSpawn, 100, 7, 0, 0);
        WorldResult<float> repeatedPlayerSampleResult =
            WorldDeterministicSampler::SampleUnit(WorldDeterministicSampleDomain::PlayerSpawn, 100, 7, 0, 0);
        WorldResult<float> ambientSampleResult =
            WorldDeterministicSampler::SampleUnit(WorldDeterministicSampleDomain::AmbientResourceSpawn, 100, 7, 0, 0);
        ASSERT_TRUE(firstPlayerSampleResult.Succeeded());
        ASSERT_TRUE(repeatedPlayerSampleResult.Succeeded());
        ASSERT_TRUE(ambientSampleResult.Succeeded());
        const float firstPlayerSample = firstPlayerSampleResult.TakeValue();
        const float repeatedPlayerSample = repeatedPlayerSampleResult.TakeValue();
        const float ambientSample = ambientSampleResult.TakeValue();

        EXPECT_FLOAT_EQ(firstPlayerSample, repeatedPlayerSample);
        EXPECT_NE(firstPlayerSample, ambientSample);
        EXPECT_GE(firstPlayerSample, 0.0f);
        EXPECT_LT(firstPlayerSample, 1.0f);
        EXPECT_GE(ambientSample, 0.0f);
        EXPECT_LT(ambientSample, 1.0f);
    }

    TEST(WorldDeterministicSamplerTests, RejectsInvalidDomain)
    {
        const WorldResult<float> result =
            WorldDeterministicSampler::SampleUnit(WorldDeterministicSampleDomain::Invalid, 100, 7, 0, 0);

        ASSERT_TRUE(result.Failed());
        EXPECT_EQ(result.Error(), WorldErrorCode::InvalidArgument);
    }
} // namespace psnr::world::tests
