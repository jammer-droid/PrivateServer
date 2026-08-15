#include "pch.h"

#include "ApplicationLogConfig.h"
#include "ApplicationLogValidation.h"

namespace psnr::logging
{
    bool ApplicationLogConfig::IsValid(const ApplicationLogConfig& config) noexcept
    {
        return internal::MatchesCanonicalRunId(config.runId) && !config.process.empty() &&
               !config.outputDirectory.empty() && internal::IsValidSeverity(config.minimumSeverity) &&
               config.queueCapacity > 0 && config.rotationBytes > 0 && config.rotationFileCount > 0 &&
               config.rotationFileCount <= MaximumApplicationLogRotationFileCount;
    }
} // namespace psnr::logging
