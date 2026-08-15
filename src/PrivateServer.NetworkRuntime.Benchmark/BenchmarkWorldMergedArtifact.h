#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace psnr::benchmark
{
    struct BenchmarkWorldMergedArtifactWriteResult final
    {
        std::string error;
        bool valid = false;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return error.empty();
        }
    };

    class BenchmarkWorldMergedArtifact final
    {
    public:
        [[nodiscard]] static BenchmarkWorldMergedArtifactWriteResult Write(const std::filesystem::path& runsRoot,
                                                                           std::string_view runId);
    };
} // namespace psnr::benchmark
