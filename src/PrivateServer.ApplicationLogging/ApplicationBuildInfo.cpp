#include "pch.h"

#include "ApplicationBuildInfo.h"

namespace psnr::logging
{
    ApplicationBuildInfo ApplicationBuildInfo::Current() noexcept
    {
        ApplicationBuildInfo info{};

#if defined(NDEBUG)
        info.configuration = "Release";
#else
        info.configuration = "Debug";
#endif

#if defined(_M_X64)
        info.architecture = "x64";
#elif defined(_M_IX86)
        info.architecture = "Win32";
#else
        info.architecture = "unknown";
#endif

#if defined(_MSC_VER)
        info.msvcVersion = _MSC_VER;
        info.msvcFullVersion = _MSC_FULL_VER;
#endif

        return info;
    }
} // namespace psnr::logging
