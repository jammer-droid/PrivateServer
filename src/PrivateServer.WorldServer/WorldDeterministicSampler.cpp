#include "pch.h"

#include "WorldDeterministicSampler.h"

namespace psnr::world
{
    namespace
    {
        constexpr float UnitScale = 1.0f / 16777216.0f;

        [[nodiscard]] std::uint32_t MixBits(std::uint32_t value) noexcept
        {
            value ^= value >> 16;
            value *= 0x7feb352du;
            value ^= value >> 15;
            value *= 0x846ca68bu;
            value ^= value >> 16;
            return value;
        }

    } // namespace

    WorldResult<float> WorldDeterministicSampler::SampleUnit(const WorldDeterministicSampleDomain domain,
                                                             const std::uint32_t serverTick,
                                                             const std::uint32_t subjectId,
                                                             const std::uint32_t candidateOrdinal,
                                                             const std::uint32_t sampleChannel) noexcept
    {
        std::uint32_t domainSalt = 0;
        if (domain == WorldDeterministicSampleDomain::PlayerSpawn)
        {
            domainSalt = 0xa511e9b3u;
        }
        else if (domain == WorldDeterministicSampleDomain::AmbientResourceSpawn)
        {
            domainSalt = 0x63d83595u;
        }
        else
        {
            return WorldResult<float>::Failure(WorldErrorCode::InvalidArgument);
        }

        const std::uint32_t baseSeed =
            MixBits(domainSalt ^ serverTick ^ MixBits(subjectId) ^ MixBits(candidateOrdinal + 0x9e3779b9u));
        const std::uint32_t channelSeed = MixBits(sampleChannel + 0x68bc21ebu);
        const std::uint32_t sampleBits = MixBits(baseSeed ^ channelSeed);

        // 32비트 난수 정수의 하위 8비트 제거
        // 24비트 정수 k를 float 으로 재인코딩
        // [sign 1] [exponent 8] [fraction 23]
        // 원래 정수 최상위 1을 제외한 fraction 23이 float 으로 표현 가능한 24비트 정수 단위
        return WorldResult<float>(static_cast<float>(sampleBits >> 8) * UnitScale);
    }
} // namespace psnr::world
