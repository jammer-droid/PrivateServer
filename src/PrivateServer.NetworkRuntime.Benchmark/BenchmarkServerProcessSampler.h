#pragma once

#include "BenchmarkServerChildProcess.h"

#include <chrono>
#include <cstdint>
#include <string>

namespace psnr::benchmark
{
    // sampling 시점의 서버 child process 의 상태를 담는 DTO
    struct BenchmarkServerProcessSampleV1 final
    {
        std::uint64_t sequence = 0;           // sampling 요청 식별자
        std::uint64_t elapsedNanoseconds = 0; // process sampler 생성 이후 현재 sample 까지 흐른 시간
        std::uint64_t kernelTime100Nanoseconds =
            0; // 서버 process가 kernel mode에서 사용한 누적 CPU 시간(IO, system call 등)
        std::uint64_t userTime100Nanoseconds = 0; // 서버 proces가 user mode 에서 사용한 누적 CPU 시간
        std::uint64_t workingSetBytes = 0;        // 현재 서버 process가 실제 물리 메모리에 올려둔 페이지 크기
        std::uint64_t privateUsageBytes = 0;      // 서버 process 전용으로 commit 된 메모리
        std::uint32_t logicalProcessorCount = 0;  // 논리 프로세서 수
        double intervalCpuPercent = 0.0;          // 이전 sample부터 현재 sample 까지 서버 process가 사용한 CPU 비율
        bool hasIntervalCpuPercent = false;
    };

    /*
    intervalCpuPercent
    CPU 비율(%) : 측정 구간 동안 서버 process가 전체 CPU 처리 능력 중 얼마를 사용했는지
        - 논리 프로세서 8개, 측정 간격 1초 = 전체 CPU가 제공할 수 있는 실행 시간은 8CPU 초
        - 이때 모든 서버 process의 모든 스레드가 합쳐서 사용한 시간을
            - 1초 -> 12.5% ( 코어 하나 완전 점유 ), 4초 -> 50%, 8초 -> 100%
    CPU % = process CPU delta / (wall time delta x logicalProcessCount) x 100
    CPU delta = (현재 kernel + 현재 user) - (이전 kernel + 이전 user)
    */

    /*
    Working Set
    - 현재 process의 페이지 중 실제 물리 RAM에 올라와 있는 양
    - 서버 heap / stack 중 현재 RAM에 있는 페이지
    - 실행 코드, DLL 페이지
    - memory-mapped 로 공유되는 파일(COW 발생하면 private memory가 될 수 있음)
    - 공유 페이지 등

    Commit Memory(PrivateUsage)
    - Windows 가 process에 사용할 수 있다고 보장한 메모리. 저장 공간은 RAM 또는 page file로 보증
    - 다른 process와 공유할 수 없음
    - 현재 RAM에 로드되지 않을 수 있음
    - page file 에 기록됐다는 의미는 아님
    */

    struct BenchmarkServerProcessSampleResult final
    {
        BenchmarkServerProcessSampleV1 sample;
        std::string error;
        std::uint32_t nativeErrorCode = 0;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return error.empty();
        }
    };

    class BenchmarkServerProcessSampler final
    {
    public:
        explicit BenchmarkServerProcessSampler(BenchmarkNativeProcessHandle processHandle) noexcept;

        [[nodiscard]] BenchmarkServerProcessSampleResult Capture(std::uint64_t sequence);

    private:
        BenchmarkNativeProcessHandle processHandle_ = nullptr; // non-owning, child process가 sampler보다 오래 유지된다.

        std::chrono::steady_clock::time_point startedAt_;
        std::chrono::steady_clock::time_point previousObservedAt_;

        std::uint64_t previousKernelTime100Nanoseconds_ = 0;
        std::uint64_t previousUserTime100Nanoseconds_ = 0;
        std::uint32_t logicalProcessorCount_ = 0; // CPU 시간 계산을 위한 논리 프로세스 개수
        std::uint32_t logicalProcessorErrorCode_ = 0;
        bool hasPreviousSample_ = false;
    };
} // namespace psnr::benchmark
