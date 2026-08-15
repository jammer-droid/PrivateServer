#include "WorldServerHostStopSignal.h"

namespace psnr::world::host
{
    std::atomic<bool> WorldServerHostStopSignal::stopRequested_ = false;

    WorldServerHostStopSignal::~WorldServerHostStopSignal()
    {
        if (registered_)
        {
            static_cast<void>(SetConsoleCtrlHandler(HandleConsoleControl, FALSE));
        }
    }

    WorldResult<void, std::uint32_t> WorldServerHostStopSignal::RegisterConsoleControl() noexcept
    {
        if (registered_)
        {
            return WorldResult<void, std::uint32_t>::Success();
        }

        stopRequested_.store(false, std::memory_order_release);
        if (SetConsoleCtrlHandler(HandleConsoleControl, TRUE) == FALSE)
        {
            return WorldResult<void, std::uint32_t>::Failure(static_cast<std::uint32_t>(GetLastError()));
        }

        registered_ = true;
        return WorldResult<void, std::uint32_t>::Success();
    }

    void WorldServerHostStopSignal::Request() noexcept
    {
        stopRequested_.store(true, std::memory_order_release);
        stopRequested_.notify_all();
    }

    bool WorldServerHostStopSignal::IsRequested() const noexcept
    {
        return stopRequested_.load(std::memory_order_acquire);
    }

    BOOL WINAPI WorldServerHostStopSignal::HandleConsoleControl(const DWORD controlType) noexcept
    {
        if (controlType != CTRL_C_EVENT && controlType != CTRL_BREAK_EVENT)
        {
            return FALSE;
        }

        stopRequested_.store(true, std::memory_order_release);
        stopRequested_.notify_all();
        return TRUE;
    }
} // namespace psnr::world::host
