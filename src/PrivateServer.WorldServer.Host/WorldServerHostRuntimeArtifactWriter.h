#pragma once

#include "WorldResult.h"
#include "WorldServerHostRuntimeSample.h"

#include <PrivateServer/NetworkRuntime/NrServerSnapshot.h>
#include <PrivateServer/NetworkRuntime/NrStatus.h>

#include <cstdint>
#include <filesystem>
#include <ostream>
#include <string_view>

namespace psnr::world::host
{
    enum class WorldServerHostRuntimeArtifactWriteError : std::uint8_t
    {
        InvalidArgument = 0,
        WriteFailed,
        RenameFailed,
    };

    class WorldServerHostRuntimeArtifactWriter final
    {
    public:
        // 이 타입은 thread를 소유하지 않는다. 통합 artifact writer worker가 호출하는
        // Runtime JSON 직렬화 및 terminal report 기록 기능만 제공한다.
        [[nodiscard]] static WorldResult<void, WorldServerHostRuntimeArtifactWriteError> WriteSampleLine(
            std::ostream& output, std::string_view runId, const WorldServerHostRuntimeSample& sample) noexcept;
        [[nodiscard]] static WorldResult<void, WorldServerHostRuntimeArtifactWriteError> Write(
            std::string_view runId, const std::filesystem::path& outputPath,
            const psnr::core::NrStatus& snapshotCaptureStatus,
            const psnr::runtime::NrServerSnapshot& snapshot) noexcept;
    };
} // namespace psnr::world::host
