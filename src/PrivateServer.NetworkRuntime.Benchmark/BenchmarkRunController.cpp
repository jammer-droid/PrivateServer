#include "BenchmarkRunController.h"

#include "BenchmarkClientWorkload.h"
#include "BenchmarkIpcCommand.h"
#include "BenchmarkIpcEvent.h"
#include "BenchmarkRunArtifact.h"
#include "BenchmarkServerChildProcess.h"
#include "BenchmarkServerProcessSampler.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace psnr::benchmark
{
    namespace
    {
        constexpr int ControllerSuccessExitCode = 0;
        constexpr int ControllerFailureExitCode = 1;

        [[nodiscard]] BenchmarkWorkloadPhase ResolveWorkloadPhase(
            const std::chrono::steady_clock::time_point observedAt, const bool hasTimeline,
            const BenchmarkWorkloadTimelineSnapshot& timeline) noexcept
        {
            if (!hasTimeline || observedAt < timeline.workloadBegin)
            {
                return BenchmarkWorkloadPhase::Connect;
            }
            if (observedAt < timeline.measurementBegin)
            {
                return BenchmarkWorkloadPhase::Warmup;
            }
            if (observedAt < timeline.measurementEnd)
            {
                return BenchmarkWorkloadPhase::Measurement;
            }
            return BenchmarkWorkloadPhase::Drain;
        }

        [[nodiscard]] int Fail(const std::string_view error)
        {
            std::cerr << "[controller] " << error << '\n';
            return ControllerFailureExitCode;
        }

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

    [[nodiscard]] static int RunAttempt(const BenchmarkConfigV1& config, const std::string_view normalizedConfigJson,
                                        const std::string_view configPath, const std::string_view baselineSetId,
                                        const std::uint32_t repeatIndex)
    {
        const BenchmarkRunArtifactCreateResult artifactResult = BenchmarkRunArtifactFactory::Create(
            config.artifact.outputRoot, normalizedConfigJson, baselineSetId, repeatIndex, config.execution.repeats);
        if (!artifactResult.Succeeded())
        {
            return Fail(artifactResult.error);
        }
        const std::string& runId = artifactResult.artifact.runId;

        // launch child sever process
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

        BenchmarkClientWorkloadResult workloadResult;
        BenchmarkWorkloadTimeline workloadTimeline;
        std::mutex workloadCompletionMutex;
        std::condition_variable workloadCompletionCondition;
        bool workloadFinished = false;
        std::jthread workloadThread; // auto join thread
        try
        {
            workloadThread = std::jthread(
                [&]()
                {
                    try
                    {
                        workloadResult = BenchmarkClientWorkload::Run(config, workloadTimeline);
                    }
                    catch (const std::exception& exception)
                    {
                        workloadResult.error = "client workload threw an exception: ";
                        workloadResult.error.append(exception.what());
                    }
                    catch (...)
                    {
                        workloadResult.error = "client workload threw an unknown exception";
                    }

                    {
                        const std::lock_guard<std::mutex> lock(workloadCompletionMutex);
                        workloadFinished = true;
                    }
                    workloadCompletionCondition.notify_one();
                });
        }
        catch (const std::exception& exception)
        {
            return Fail(std::string("failed to start client workload thread: ") + exception.what());
        }

        std::vector<BenchmarkPeriodicSampleV1> periodicSamples;
        BenchmarkWorkloadTimelineSnapshot workloadTimelineSnapshot;
        bool hasWorkloadTimeline = false;
        BenchmarkServerProcessSampler processSampler(child.ProcessHandle());
        std::uint64_t nextCommandSequence = 1;
        const std::chrono::milliseconds samplingInterval{config.sampling.intervalMs};
        std::chrono::steady_clock::time_point nextSampleAt =
            std::chrono::steady_clock::now(); // runtime snapshot 요청 시간
        std::string samplingError;
        while (true)
        {
            {
                std::unique_lock<std::mutex> lock(workloadCompletionMutex);
                // wait until
                // - workloadFinished == true
                // - nextSampleAt 도달(timeout)
                if (workloadCompletionCondition.wait_until(lock, nextSampleAt,
                                                           [&]() noexcept { return workloadFinished; }))
                {
                    break;
                }
            }

            const std::uint64_t sampleSequence = nextCommandSequence++;
            const std::chrono::steady_clock::time_point sampleObservedAt = std::chrono::steady_clock::now();
            if (!hasWorkloadTimeline)
            {
                hasWorkloadTimeline = workloadTimeline.TryRead(&workloadTimelineSnapshot);
            }
            const BenchmarkWorkloadPhase samplePhase =
                ResolveWorkloadPhase(sampleObservedAt, hasWorkloadTimeline, workloadTimelineSnapshot);

            BenchmarkServerProcessSampleResult processSampleResult = processSampler.Capture(sampleSequence);
            if (!processSampleResult.Succeeded())
            {
                samplingError = processSampleResult.error;
                break;
            }

            BenchmarkIpcCommandV1 sampleCommand;
            sampleCommand.type = BenchmarkIpcCommandType::CaptureSnapshot;
            sampleCommand.runId = runId;
            sampleCommand.sequence = sampleSequence;
            const BenchmarkIpcCommandEncodeResult sampleEncodeResult =
                BenchmarkIpcCommandV1Codec::Encode(sampleCommand);
            if (!sampleEncodeResult.Succeeded())
            {
                samplingError = sampleEncodeResult.error;
                break;
            }

            // ServerSnapshot 요청 command 전달
            const BenchmarkIpcIoResult sampleWriteResult = child.CommandWriter().WriteLine(sampleEncodeResult.json);
            if (!sampleWriteResult.Succeeded())
            {
                samplingError = sampleWriteResult.error;
                break;
            }
            if (!ReadEvent(child, runId, &event, &samplingError))
            {
                break;
            }
            if (event.type != BenchmarkIpcEventType::RuntimeSample || event.sequence != sampleSequence)
            {
                samplingError = "Server child did not publish the expected runtime sample event";
                break;
            }
            BenchmarkPeriodicSampleV1 periodicSample;
            periodicSample.sequence = sampleSequence;
            periodicSample.phase = samplePhase;
            periodicSample.process = std::move(processSampleResult.sample);
            periodicSample.runtime = std::move(event.runtimeSample);
            periodicSamples.push_back(std::move(periodicSample));

            nextSampleAt += samplingInterval;
            const std::chrono::steady_clock::time_point observedAt = std::chrono::steady_clock::now();
            while (nextSampleAt <= observedAt)
            {
                nextSampleAt += samplingInterval;
            }
        }
        workloadThread.join();

        BenchmarkIpcCommandV1 stopCommand;
        stopCommand.type = BenchmarkIpcCommandType::Stop;
        stopCommand.runId = runId;
        stopCommand.sequence = nextCommandSequence;
        const BenchmarkIpcCommandEncodeResult encodeResult = BenchmarkIpcCommandV1Codec::Encode(stopCommand);
        if (!encodeResult.Succeeded())
        {
            return Fail(encodeResult.error);
        }

        const BenchmarkIpcIoResult writeResult = child.CommandWriter().WriteLine(encodeResult.json); // shutdown command
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
        if (timeoutMilliseconds > std::numeric_limits<std::uint32_t>::max())
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

        bool hasMeasurementSample = false;
        for (const BenchmarkPeriodicSampleV1& sample : periodicSamples)
        {
            if (sample.phase == BenchmarkWorkloadPhase::Measurement)
            {
                hasMeasurementSample = true;
                break;
            }
        }

        BenchmarkRunCompletionV1 completion;
        if (!samplingError.empty())
        {
            completion.error = samplingError;
        }
        else if (!workloadResult.Succeeded())
        {
            completion.error = workloadResult.error;
        }
        else if (!hasMeasurementSample)
        {
            completion.error = "measurement phase has no periodic process/runtime sample";
        }
        else if (workloadResult.applicationObservedRttNanoseconds.empty() ||
                 workloadResult.serverProcessingDurationNanoseconds.empty() ||
                 workloadResult.schedulerLagNanoseconds.empty())
        {
            completion.error = "measurement phase has no latency evidence";
        }
        completion.valid = completion.error.empty();
        const BenchmarkRunArtifactWriteResult artifactWriteResult = BenchmarkRunArtifactWriter::WriteRunEvidence(
            artifactResult.artifact, periodicSamples, workloadResult, completion);
        if (!artifactWriteResult.Succeeded())
        {
            return Fail(artifactWriteResult.error);
        }
        if (!completion.valid)
        {
            return Fail(completion.error);
        }

        const std::uint64_t plannedRequestCount = workloadResult.accepted + workloadResult.missedSchedule;
        const double scheduleFulfillmentRatio =
            plannedRequestCount == 0
                ? 1.0
                : static_cast<double>(workloadResult.accepted) / static_cast<double>(plannedRequestCount);
        std::cout << "[controller] workload result: planned=" << plannedRequestCount
                  << " accepted=" << workloadResult.accepted << " completed=" << workloadResult.completed
                  << " missedSchedule=" << workloadResult.missedSchedule
                  << " scheduleFulfillmentRatio=" << scheduleFulfillmentRatio << '\n';
        std::size_t connectSampleCount = 0;
        std::size_t warmupSampleCount = 0;
        std::size_t measurementSampleCount = 0;
        std::size_t drainSampleCount = 0;
        for (const BenchmarkPeriodicSampleV1& sample : periodicSamples)
        {
            switch (sample.phase)
            {
            case BenchmarkWorkloadPhase::Connect:
                ++connectSampleCount;
                break;
            case BenchmarkWorkloadPhase::Warmup:
                ++warmupSampleCount;
                break;
            case BenchmarkWorkloadPhase::Measurement:
                ++measurementSampleCount;
                break;
            case BenchmarkWorkloadPhase::Drain:
                ++drainSampleCount;
                break;
            }
        }
        std::cout << "[controller] runtime snapshots captured=" << periodicSamples.size() << '\n';
        std::cout << "[controller] process samples captured=" << periodicSamples.size() << '\n';
        std::cout << "[controller] phase samples: connect=" << connectSampleCount << " warmup=" << warmupSampleCount
                  << " measurement=" << measurementSampleCount << " drain=" << drainSampleCount << '\n';
        std::cout << "[controller] fixed-rate public NrClient workload completed: baselineSetId=" << baselineSetId
                  << " repeat=" << repeatIndex << '/' << config.execution.repeats << " runId=" << runId << '\n';
        return ControllerSuccessExitCode;
    }

    int BenchmarkRunController::Run(const BenchmarkConfigV1& config, const std::string_view normalizedConfigJson,
                                    const std::string_view configPath)
    {
        std::string baselineSetId;
        if (!BenchmarkRunArtifactFactory::TryCreateBaselineSetId(&baselineSetId))
        {
            return Fail("failed to create benchmark baselineSetId");
        }

        int setExitCode = ControllerSuccessExitCode;
        // 반복 실행
        for (std::uint32_t repeatOffset = 0; repeatOffset < config.execution.repeats; ++repeatOffset)
        {
            const std::uint32_t repeatIndex = repeatOffset + 1;
            std::cout << "[controller] starting baseline attempt: baselineSetId=" << baselineSetId
                      << " repeat=" << repeatIndex << '/' << config.execution.repeats << '\n';

            const int attemptExitCode =
                RunAttempt(config, normalizedConfigJson, configPath, baselineSetId, repeatIndex);
            if (attemptExitCode != ControllerSuccessExitCode)
            {
                setExitCode = ControllerFailureExitCode;
            }
        }
        return setExitCode;
    }
} // namespace psnr::benchmark
