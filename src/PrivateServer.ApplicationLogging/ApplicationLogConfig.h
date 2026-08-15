#pragma once

#include "ApplicationLogSeverity.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace psnr::logging
{
    // 비동기 logger queue 에 최대 보관할 record 개수, 초과하면 discard_new
    inline constexpr std::size_t DefaultApplicationLogQueueCapacity = 8'192;

    // 로그 파일이 rotation 기준 바이트(default = 10MiB)
    inline constexpr std::uint64_t DefaultApplicationLogRotationBytes = 10ULL * 1024ULL * 1024ULL;

    // Rotation 된 log segment 는 최대 5개 유지
    inline constexpr std::size_t MaximumApplicationLogRotationFileCount = 5;
    inline constexpr std::size_t DefaultApplicationLogRotationFileCount = MaximumApplicationLogRotationFileCount;

    struct ApplicationLogConfig final
    {
        [[nodiscard]] static bool IsValid(const ApplicationLogConfig& config) noexcept;

        std::string runId;
        std::string process;
        std::filesystem::path outputDirectory;
        ApplicationLogSeverity minimumSeverity = ApplicationLogSeverity::Info;
        std::size_t queueCapacity = DefaultApplicationLogQueueCapacity;
        std::uint64_t rotationBytes = DefaultApplicationLogRotationBytes;
        std::size_t rotationFileCount = DefaultApplicationLogRotationFileCount;
    };
} // namespace psnr::logging
