#include "BenchmarkServerProcessSampler.h"

#include <Windows.h>
#include <Psapi.h>

#include <algorithm>
#include <chrono>
#include <cstdint>

namespace psnr::benchmark
{
    namespace
    {
        [[nodiscard]] std::uint64_t FileTimeTo100Nanoseconds(const FILETIME& fileTime) noexcept
        {
            ULARGE_INTEGER value;
            value.LowPart = fileTime.dwLowDateTime;
            value.HighPart = fileTime.dwHighDateTime;
            return value.QuadPart;
        }

        [[nodiscard]] BenchmarkServerProcessSampleResult CaptureFailure(const char* const error,
                                                                        const std::uint32_t nativeErrorCode = 0)
        {
            BenchmarkServerProcessSampleResult result;
            result.error = error;
            result.nativeErrorCode = nativeErrorCode;
            return result;
        }
    } // namespace

    BenchmarkServerProcessSampler::BenchmarkServerProcessSampler(
        const BenchmarkNativeProcessHandle processHandle) noexcept
        : processHandle_(processHandle)
        , startedAt_(std::chrono::steady_clock::now())
    {
        logicalProcessorCount_ = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
        if (logicalProcessorCount_ == 0)
        {
            logicalProcessorErrorCode_ = GetLastError();
        }
    }

    BenchmarkServerProcessSampleResult BenchmarkServerProcessSampler::Capture(const std::uint64_t sequence)
    {
        const HANDLE processHandle = static_cast<HANDLE>(processHandle_);
        if (processHandle == nullptr || processHandle == INVALID_HANDLE_VALUE)
        {
            return CaptureFailure("Server child process handle is invalid");
        }
        if (sequence == 0)
        {
            return CaptureFailure("Server process sample sequence must be positive");
        }
        if (logicalProcessorCount_ == 0)
        {
            return CaptureFailure("failed to read the active logical processor count", logicalProcessorErrorCode_);
        }

        FILETIME creationTime{};
        FILETIME exitTime{};
        FILETIME kernelTime{};
        FILETIME userTime{};
        if (GetProcessTimes(processHandle, &creationTime, &exitTime, &kernelTime, &userTime) == FALSE)
        {
            return CaptureFailure("failed to read Server child process CPU times", GetLastError());
        }

        PROCESS_MEMORY_COUNTERS_EX memoryCounters{};
        memoryCounters.cb = static_cast<DWORD>(sizeof(memoryCounters));
        if (GetProcessMemoryInfo(processHandle, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memoryCounters),
                                 static_cast<DWORD>(sizeof(memoryCounters))) == FALSE)
        {
            return CaptureFailure("failed to read Server child process memory counters", GetLastError());
        }

        const std::chrono::steady_clock::time_point observedAt = std::chrono::steady_clock::now();
        const std::uint64_t kernelTime100Nanoseconds = FileTimeTo100Nanoseconds(kernelTime);
        const std::uint64_t userTime100Nanoseconds = FileTimeTo100Nanoseconds(userTime);
        if (hasPreviousSample_ && (kernelTime100Nanoseconds < previousKernelTime100Nanoseconds_ ||
                                   userTime100Nanoseconds < previousUserTime100Nanoseconds_))
        {
            return CaptureFailure("Server child process CPU time moved backwards");
        }

        BenchmarkServerProcessSampleResult result;
        BenchmarkServerProcessSampleV1& sample = result.sample;
        sample.sequence = sequence;
        sample.elapsedNanoseconds = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(observedAt - startedAt_).count());
        sample.kernelTime100Nanoseconds = kernelTime100Nanoseconds;
        sample.userTime100Nanoseconds = userTime100Nanoseconds;
        sample.workingSetBytes = static_cast<std::uint64_t>(memoryCounters.WorkingSetSize);
        sample.privateUsageBytes = static_cast<std::uint64_t>(memoryCounters.PrivateUsage);
        sample.logicalProcessorCount = logicalProcessorCount_;

        if (hasPreviousSample_ && observedAt > previousObservedAt_)
        {
            const std::uint64_t kernelDelta = kernelTime100Nanoseconds - previousKernelTime100Nanoseconds_;
            const std::uint64_t userDelta = userTime100Nanoseconds - previousUserTime100Nanoseconds_;
            const double cpuSeconds = static_cast<double>(kernelDelta + userDelta) / 10'000'000.0;
            const std::chrono::duration<double> wallInterval = observedAt - previousObservedAt_;
            const double normalizedCpuPercent =
                cpuSeconds / wallInterval.count() / static_cast<double>(logicalProcessorCount_) * 100.0;
            sample.intervalCpuPercent = std::clamp(normalizedCpuPercent, 0.0, 100.0);
            sample.hasIntervalCpuPercent = true;
        }

        previousObservedAt_ = observedAt;
        previousKernelTime100Nanoseconds_ = kernelTime100Nanoseconds;
        previousUserTime100Nanoseconds_ = userTime100Nanoseconds;
        hasPreviousSample_ = true;
        return result;
    }
} // namespace psnr::benchmark
