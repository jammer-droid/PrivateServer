#pragma once

#include "WorldTickSampleBuffer.h"

#include <cstdint>
#include <memory>

namespace psnr::world
{
    enum class WorldTickSampleBatchCompleteness : std::uint8_t
    {
        Invalid = 0,
        Complete,
        Incomplete,
    };

    // 종료된 라운드 버퍼는 이 경계를 통해 이동한다. 수신자는 비동기 artifact 기록이
    // 끝날 때까지 버퍼의 소유권을 가진다.
    struct WorldTickSampleBatch final
    {
        std::unique_ptr<WorldTickSampleBuffer> samples;
        WorldTickSampleBatchCompleteness completeness = WorldTickSampleBatchCompleteness::Invalid;
    };

    enum class WorldTickSampleSinkResult : std::uint8_t
    {
        Succeeded = 0,
        Full,
        Closed,
        InvalidArgument,
    };

    // World는 라운드 샘플이 이 경계를 통해 simulation thread를 벗어난다는 계약만 안다.
    // queue 처리와 파일 I/O의 소유권은 Host가 가진다.
    class IWorldTickSampleSink
    {
    public:
        virtual ~IWorldTickSampleSink() noexcept = default;

        [[nodiscard]] virtual WorldTickSampleSinkResult TrySubmit(
            std::unique_ptr<WorldTickSampleBuffer>&& samples,
            WorldTickSampleBatchCompleteness completeness) noexcept = 0;
    };
} // namespace psnr::world
