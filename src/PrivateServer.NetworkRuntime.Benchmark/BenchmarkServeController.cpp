#include "BenchmarkServeController.h"

#include "BenchmarkIpcCommand.h"
#include "BenchmarkIpcEvent.h"
#include "BenchmarkRunArtifact.h"
#include "BenchmarkServerChildProcess.h"
#include "BenchmarkServerProcessSampler.h"

#include <Windows.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>

namespace psnr::benchmark
{
    namespace
    {
        constexpr int ControllerSuccessExitCode = 0;
        constexpr int ControllerFailureExitCode = 1;
        constexpr DWORD ChildObservationIntervalMilliseconds = 50;

        std::atomic<bool> stopRequested = false;

        [[nodiscard]] int Fail(const std::string_view error)
        {
            std::cerr << "[controller] " << error << '\n';
            return ControllerFailureExitCode;
        }

        BOOL WINAPI HandleConsoleControl(const DWORD controlType) noexcept
        {
            if (controlType != CTRL_C_EVENT) // 종료 신호 확인(ctrl + c)
            {
                return FALSE;
            }

            stopRequested.store(true, std::memory_order_relaxed);
            return TRUE; // controlType의 console event 처리 완료
        }

        class ConsoleControlRegistration final
        {
        public:
            ConsoleControlRegistration() noexcept
                : registered_(SetConsoleCtrlHandler(HandleConsoleControl, TRUE) != FALSE)
            {
                // SetConsoleCtrlHandler
                // - console event callback 함수 등록
            }

            ~ConsoleControlRegistration() noexcept
            {
                if (registered_)
                {
                    // console event callback 해제는 FALSE를 사용
                    static_cast<void>(SetConsoleCtrlHandler(HandleConsoleControl, FALSE));
                }
            }

            ConsoleControlRegistration(const ConsoleControlRegistration&) = delete;
            ConsoleControlRegistration& operator=(const ConsoleControlRegistration&) = delete;

            ConsoleControlRegistration(ConsoleControlRegistration&&) = delete;
            ConsoleControlRegistration& operator=(ConsoleControlRegistration&&) = delete;

            [[nodiscard]] bool Registered() const noexcept
            {
                return registered_;
            }

        private:
            bool registered_ = false;
        };

        [[nodiscard]] bool ReadEvent(BenchmarkServerChildProcess& child, const std::string_view expectedRunId,
                                     BenchmarkIpcEventV1* const outEvent, std::string* const outError)
        {
            if (outEvent == nullptr || outError == nullptr)
            {
                return false;
            }

            std::string line;
            const BenchmarkIpcReadResult readResult = child.EventReader().ReadLine(&line);
            if (!readResult.Succeeded())
            {
                *outError = readResult.error;
                return false;
            }
            if (readResult.outcome == BenchmarkIpcReadOutcome::EndOfStream)
            {
                *outError = "Server child event pipe ended before the expected event";
                return false;
            }

            const BenchmarkIpcEventDecodeResult decodeResult = BenchmarkIpcEventV1Codec::Decode(line);
            if (!decodeResult.Succeeded())
            {
                *outError = decodeResult.error;
                return false;
            }
            if (decodeResult.event.runId != expectedRunId)
            {
                *outError = "Server child event runId does not match Controller runId";
                return false;
            }
            if (decodeResult.event.type == BenchmarkIpcEventType::Error)
            {
                *outError = decodeResult.event.errorMessage;
                return false;
            }

            *outEvent = decodeResult.event;
            return true;
        }
    } // namespace

    int BenchmarkServeController::Run(const BenchmarkConfigV1& config, const std::string_view normalizedConfigJson,
                                      const std::string_view configPath)
    {
        std::string serveSetId;
        if (!BenchmarkRunArtifactFactory::TryCreateBaselineSetId(&serveSetId))
        {
            return Fail("failed to create serve observation set id");
        }

        const BenchmarkRunArtifactCreateResult artifactResult =
            BenchmarkRunArtifactFactory::Create(config.artifact.outputRoot, normalizedConfigJson, serveSetId, 1, 1);
        if (!artifactResult.Succeeded())
        {
            return Fail(artifactResult.error);
        }
        const std::string& runId = artifactResult.artifact.runId;

        stopRequested.store(false, std::memory_order_relaxed);
        const ConsoleControlRegistration consoleControlRegistration;
        if (!consoleControlRegistration.Registered())
        {
            return Fail("failed to register the Controller console control handler");
        }

        const BenchmarkServerChildLaunchResult launchResult = BenchmarkServerChildProcess::Launch(runId, configPath);
        if (!launchResult.Succeeded())
        {
            return Fail(launchResult.error);
        }

        BenchmarkServerChildProcess& child = *launchResult.child;
        BenchmarkIpcEventV1 event;
        std::string error;
        if (!ReadEvent(child, runId, &event, &error))
        {
            return Fail(error);
        }
        if (event.type != BenchmarkIpcEventType::Ready || event.sequence != 0)
        {
            return Fail("Server child did not publish the expected ready event");
        }

        BenchmarkServeArtifactWriter artifactWriter;
        const BenchmarkRunArtifactWriteResult artifactStartResult = artifactWriter.Start(artifactResult.artifact);
        if (!artifactStartResult.Succeeded())
        {
            return Fail(artifactStartResult.error);
        }

        std::cout << "[controller] serve ready: runId=" << runId << " endpoint=" << config.server.address << ':'
                  << config.server.port << '\n';
        std::cout << "[controller] press Ctrl+C to stop the Server child gracefully\n";

        BenchmarkServerProcessSampler processSampler(child.ProcessHandle());
        std::uint64_t nextCommandSequence = 1;
        const std::chrono::milliseconds samplingInterval{config.sampling.intervalMs};
        std::chrono::steady_clock::time_point nextSampleAt = std::chrono::steady_clock::now();
        while (!stopRequested.load(std::memory_order_relaxed)) // 사용자 ctrl + c 입력 전에
        {
            // serve mode benchmark 측정
            const std::chrono::steady_clock::time_point observedAt = std::chrono::steady_clock::now();
            if (observedAt >= nextSampleAt)
            {
                const std::uint64_t sampleSequence = nextCommandSequence++;
                const BenchmarkServerProcessSampleResult processSampleResult = processSampler.Capture(sampleSequence);
                if (!processSampleResult.Succeeded())
                {
                    return Fail(processSampleResult.error);
                }

                BenchmarkIpcCommandV1 sampleCommand;
                sampleCommand.type = BenchmarkIpcCommandType::CaptureSnapshot;
                sampleCommand.runId = runId;
                sampleCommand.sequence = sampleSequence;
                const BenchmarkIpcCommandEncodeResult sampleEncodeResult =
                    BenchmarkIpcCommandV1Codec::Encode(sampleCommand);
                if (!sampleEncodeResult.Succeeded())
                {
                    return Fail(sampleEncodeResult.error);
                }

                // send CaptureSnapsot command
                const BenchmarkIpcIoResult sampleWriteResult = child.CommandWriter().WriteLine(sampleEncodeResult.json);
                if (!sampleWriteResult.Succeeded())
                {
                    return Fail(sampleWriteResult.error);
                }
                if (!ReadEvent(child, runId, &event, &error))
                {
                    return Fail(error);
                }
                if (event.type != BenchmarkIpcEventType::RuntimeSample || event.sequence != sampleSequence)
                {
                    return Fail("Server child did not publish the expected runtime sample event");
                }

                // append process , snapshot benchmark result
                const BenchmarkRunArtifactWriteResult sampleArtifactResult = artifactWriter.AppendPeriodicSample(
                    sampleSequence, processSampleResult.sample, event.runtimeSample);
                if (!sampleArtifactResult.Succeeded())
                {
                    return Fail(sampleArtifactResult.error);
                }

                nextSampleAt += samplingInterval;
                const std::chrono::steady_clock::time_point sampleCompletedAt = std::chrono::steady_clock::now();
                while (nextSampleAt <= sampleCompletedAt)
                {
                    nextSampleAt += samplingInterval;
                }
            }

            // 종료 대기를 다음 sampling 시간이 오기 전에 실제 필요한 만큼만
            DWORD waitTimeoutMilliseconds = ChildObservationIntervalMilliseconds; // 기본 50ms 대기
            const std::chrono::steady_clock::time_point waitObservedAt = std::chrono::steady_clock::now();
            if (nextSampleAt <= waitObservedAt) // 이미 sampling 시간이 도달함
            {
                waitTimeoutMilliseconds = 0; // child process 상태만 조회하도록 대기 시간 0
            }
            else
            {
                // sampling 시간에 영향 안주도록 단축
                const std::chrono::milliseconds untilNextSample =
                    std::chrono::duration_cast<std::chrono::milliseconds>(nextSampleAt - waitObservedAt);
                if (untilNextSample.count() < static_cast<std::int64_t>(waitTimeoutMilliseconds))
                {
                    waitTimeoutMilliseconds = static_cast<DWORD>(untilNextSample.count());
                }
            }

            // child process 가 종료된 경우에 대하여 child 종료 여부 및 exit code 확인
            // child process 종료 여부 대기(최대 ChildObservationIntervalMilliseconds 대기)
            const DWORD waitResult =
                WaitForSingleObject(static_cast<HANDLE>(child.ProcessHandle()), waitTimeoutMilliseconds);
            if (waitResult == WAIT_TIMEOUT) // ChildObservationIntervalMilliseconds 동안 아직 실행
            {
                continue; // 실행 중이면 다시 sampling 으로
            }

            // 종료된 경우
            if (waitResult != WAIT_OBJECT_0) // WAIT_OBJECT_0 : child가 ChildObservationIntervalMilliseconds 내로 종료됨
            {
                return Fail("failed while observing the Server child process");
            }

            const BenchmarkServerChildExitResult exitResult = child.WaitForExit(0); // exit code 확인
            if (!exitResult.Succeeded())
            {
                return Fail(exitResult.error);
            }
            return Fail("Server child exited before the Controller received a stop request; exitCode=" +
                        std::to_string(exitResult.exitCode));
        }

        BenchmarkIpcCommandV1 stopCommand;
        stopCommand.type = BenchmarkIpcCommandType::Stop;
        stopCommand.runId = runId;
        stopCommand.sequence = nextCommandSequence;
        const BenchmarkIpcCommandEncodeResult encodeResult = BenchmarkIpcCommandV1Codec::Encode(stopCommand);
        if (!encodeResult.Succeeded())
        {
            return Fail(encodeResult.error);
        }

        // send Stop Command
        const BenchmarkIpcIoResult writeResult = child.CommandWriter().WriteLine(encodeResult.json);
        if (!writeResult.Succeeded())
        {
            return Fail(writeResult.error);
        }
        if (!ReadEvent(child, runId, &event, &error))
        {
            return Fail(error);
        }
        if (event.type != BenchmarkIpcEventType::Stopped || event.sequence != stopCommand.sequence)
        {
            return Fail("Server child did not publish the expected stopped event");
        }

        const std::uint64_t timeoutMilliseconds =
            static_cast<std::uint64_t>(config.phases.shutdownTimeoutSeconds) * 1'000;
        if (timeoutMilliseconds > (std::numeric_limits<std::uint32_t>::max)())
        {
            return Fail("shutdown timeout exceeds the supported Win32 wait range");
        }

        const BenchmarkServerChildExitResult exitResult =
            child.WaitForExit(static_cast<std::uint32_t>(timeoutMilliseconds));
        if (!exitResult.Succeeded())
        {
            return Fail(exitResult.error);
        }
        if (exitResult.exitCode != 0)
        {
            return Fail("Server child returned a non-zero exit code");
        }

        const BenchmarkRunArtifactWriteResult artifactCompleteResult = artifactWriter.Complete();
        if (!artifactCompleteResult.Succeeded())
        {
            return Fail(artifactCompleteResult.error);
        }

        std::cout << "[controller] serve stopped gracefully: runId=" << runId << '\n';
        return ControllerSuccessExitCode;
    }
} // namespace psnr::benchmark
