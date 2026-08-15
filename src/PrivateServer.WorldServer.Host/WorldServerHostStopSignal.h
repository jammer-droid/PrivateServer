#pragma once

#include "WorldResult.h"

#include <Windows.h>

#include <atomic>
#include <cstdint>

namespace psnr::world::host
{
    class WorldServerHostStopSignal final
    {
    public:
        WorldServerHostStopSignal() noexcept = default;
        ~WorldServerHostStopSignal();

        WorldServerHostStopSignal(const WorldServerHostStopSignal&) = delete;
        WorldServerHostStopSignal& operator=(const WorldServerHostStopSignal&) = delete;

        [[nodiscard]] WorldResult<void, std::uint32_t> RegisterConsoleControl() noexcept;
        void Request() noexcept;
        [[nodiscard]] bool IsRequested() const noexcept;

    private:
        static BOOL WINAPI HandleConsoleControl(DWORD controlType) noexcept;

        static std::atomic<bool> stopRequested_;
        bool registered_ = false;
    };
} // namespace psnr::world::host
