#pragma once

#include "ApplicationLogRecord.h"

namespace psnr::logging
{
    class ApplicationLogger;

    // Copy-constructible, non-owning producer capability. The Host-owned logger must outlive every handle use.
    class ApplicationLogHandle final
    {
    public:
        ApplicationLogHandle(const ApplicationLogHandle&) noexcept = default;
        ApplicationLogHandle& operator=(const ApplicationLogHandle&) = delete;

        void Log(const ApplicationLogRecord& record) const noexcept;

    private:
        friend class ApplicationLogger;

        explicit ApplicationLogHandle(ApplicationLogger& logger) noexcept;

        ApplicationLogger& logger_;
    };
} // namespace psnr::logging
