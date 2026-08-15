#pragma once

#include "ApplicationLogConfig.h"
#include "ApplicationLogHandle.h"
#include "ApplicationLogHealth.h"
#include "ApplicationLogRecord.h"
#include "ApplicationLoggerResult.h"

#include <memory>

namespace psnr::logging::internal
{
    class ApplicationLoggerTestAccess;
    class IApplicationLogOutput;
} // namespace psnr::logging::internal

namespace psnr::logging
{
    class ApplicationLogger final
    {
    public:
        ~ApplicationLogger() noexcept;

        ApplicationLogger(const ApplicationLogger&) = delete;
        ApplicationLogger& operator=(const ApplicationLogger&) = delete;

        ApplicationLogger(ApplicationLogger&&) = delete;
        ApplicationLogger& operator=(ApplicationLogger&&) = delete;

        [[nodiscard]] static ApplicationLoggerResult Create(const ApplicationLogConfig& config,
                                                            std::unique_ptr<ApplicationLogger>* outLogger) noexcept;

        [[nodiscard]] ApplicationLoggerResult Start() noexcept;
        [[nodiscard]] ApplicationLogHandle CreateHandle() noexcept;
        void Log(const ApplicationLogRecord& record) noexcept;
        [[nodiscard]] ApplicationLogHealth CaptureHealth() const noexcept;
        void Shutdown() noexcept;

    private:
        friend class internal::ApplicationLoggerTestAccess;

        class Impl;

        explicit ApplicationLogger(std::unique_ptr<Impl> impl) noexcept;

        [[nodiscard]] static std::unique_ptr<ApplicationLogger> CreateWithOutputs(
            const ApplicationLogConfig& config, std::unique_ptr<internal::IApplicationLogOutput> fileOutput,
            std::unique_ptr<internal::IApplicationLogOutput> consoleOutput) noexcept;

        std::unique_ptr<Impl> impl_;
    };
} // namespace psnr::logging
