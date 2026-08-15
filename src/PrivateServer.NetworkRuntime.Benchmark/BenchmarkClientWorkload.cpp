#include "BenchmarkClientWorkload.h"

#include "BenchmarkClientTransport.h"
#include "BenchmarkEndpointParser.h"
#include "BenchmarkProtocol.h"

#include <PrivateServer/NetworkRuntime/NrClient.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace psnr::benchmark
{
    namespace
    {
        constexpr std::chrono::milliseconds EventPollInterval{BenchmarkEventPollIntervalMilliseconds};
        constexpr std::chrono::milliseconds WorkerStartLeadTime{100};

        struct OutstandingRequest final
        {
            std::chrono::steady_clock::time_point scheduledAt;
            std::chrono::steady_clock::time_point submittedAt;
            std::uint64_t clientSendTimestampNanoseconds = 0;
            bool recordMeasurement = false;
        };

        struct BenchmarkLoadClient final
        {
            BenchmarkLoadClient(const std::uint32_t clientIdValue, psnr::runtime::NrClient&& clientValue) noexcept
                : clientId(clientIdValue)
                , client(std::move(clientValue))
            {
            }

            std::uint32_t clientId = 0;
            psnr::runtime::NrClient client;
            std::uint64_t nextSequence = 1;
            std::unordered_map<std::uint64_t, OutstandingRequest> outstandingRequests;
        };

        [[nodiscard]] BenchmarkClientWorkloadResult Failure(std::string error)
        {
            BenchmarkClientWorkloadResult result;
            result.error = std::move(error);
            return result;
        }

        [[nodiscard]] BenchmarkClientWorkloadResult Failure(BenchmarkClientWorkloadResult result, std::string error)
        {
            result.error = std::move(error);
            return result;
        }

        [[nodiscard]] std::uint64_t SteadyNanosecondsNow() noexcept
        {
            const std::int64_t nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                 std::chrono::steady_clock::now().time_since_epoch())
                                                 .count();
            return nanoseconds > 0 ? static_cast<std::uint64_t>(nanoseconds) : 0;
        }

        [[nodiscard]] bool TryDurationNanoseconds(const std::uint64_t begin, const std::uint64_t end,
                                                  std::int64_t* const outDuration) noexcept
        {
            if (outDuration == nullptr || end < begin)
            {
                return false;
            }
            const std::uint64_t duration = end - begin;
            if (duration > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()))
            {
                return false;
            }
            *outDuration = static_cast<std::int64_t>(duration);
            return true;
        }

        [[nodiscard]] bool WaitForEvent(psnr::runtime::NrClient& client,
                                        const psnr::runtime::NrClientEventKind expectedKind,
                                        const std::chrono::steady_clock::time_point deadline,
                                        psnr::runtime::NrClientEvent* const outEvent, std::string* const outError)
        {
            if (outEvent == nullptr || outError == nullptr)
            {
                return false;
            }

            *outError = BenchmarkClientTransport::ReadNextEventUntil(client, deadline, outEvent);
            if (!outError->empty())
            {
                return false;
            }
            if (outEvent->Kind() != expectedKind)
            {
                *outError = "NrClient received an unexpected event kind";
                return false;
            }
            return true;
        }

        [[nodiscard]] bool CompleteResponse(BenchmarkLoadClient& client, const psnr::runtime::NrClientEvent& event,
                                            BenchmarkClientWorkloadResult* const outResult, std::string* const outError)
        {
            psnr::core::NrPacketType packetType;
            psnr::runtime::NrByteView payload;
            const psnr::core::NrStatus packetTypeStatus = event.GetPacketType(&packetType);
            const psnr::core::NrStatus payloadStatus = event.GetPayload(&payload);

            // metadata 불일치
            if (packetTypeStatus.Failed() || payloadStatus.Failed() ||
                packetType != psnr::core::NrPacketType{BenchmarkResponsePacketType} ||
                payload.size != BenchmarkCanonicalPayloadBytes || payload.data == nullptr)
            {
                *outError = "Echo response fields do not satisfy benchmark protocol v2";
                return false;
            }

            BenchmarkProtocolCodec::CanonicalPayload canonicalPayload{};
            std::copy_n(payload.data, canonicalPayload.size(), canonicalPayload.begin());
            const BenchmarkPayload response = BenchmarkProtocolCodec::DecodeCanonical(canonicalPayload);

            // benchmark 계약 불일치
            if (response.protocolVersion != BenchmarkProtocolVersion ||
                response.operation != BenchmarkOperation::Echo || response.clientId != client.clientId ||
                response.clientSendTimestampNanoseconds == 0 || response.serverReceivedTimestampNanoseconds == 0 ||
                response.serverResponsePreparedTimestampNanoseconds < response.serverReceivedTimestampNanoseconds ||
                !BenchmarkProtocolCodec::HasDeterministicPadding(canonicalPayload))
            {
                *outError = "Echo response does not satisfy benchmark protocol v2";
                return false;
            }

            const std::unordered_map<std::uint64_t, OutstandingRequest>::iterator requestIterator =
                client.outstandingRequests.find(response.sequence);

            // payload 요청 sequence 불일치
            if (requestIterator == client.outstandingRequests.end())
            {
                *outError = "Echo response sequence is unknown or duplicated";
                return false;
            }

            const std::uint64_t clientObservedTimestampNanoseconds = SteadyNanosecondsNow();
            const OutstandingRequest& request = requestIterator->second;
            if (response.clientSendTimestampNanoseconds != request.clientSendTimestampNanoseconds)
            {
                *outError = "Echo response client timestamp does not match the outstanding request";
                return false;
            }
            if (request.recordMeasurement)
            {
                // schedulerLag = 요청을 제출한 시간 - 요청이 스케줄된 시간
                //      - 클라이언트에서 서버로 요청을 제출하고, 서버 내부에서 스케줄 처리되기까지의 시간
                outResult->schedulerLagNanoseconds.push_back(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(request.submittedAt - request.scheduledAt)
                        .count());
                std::int64_t applicationObservedRttNanoseconds = 0;
                std::int64_t serverProcessingDurationNanoseconds = 0;
                if (!TryDurationNanoseconds(request.clientSendTimestampNanoseconds, clientObservedTimestampNanoseconds,
                                            &applicationObservedRttNanoseconds) ||
                    !TryDurationNanoseconds(response.serverReceivedTimestampNanoseconds,
                                            response.serverResponsePreparedTimestampNanoseconds,
                                            &serverProcessingDurationNanoseconds))
                {
                    *outError = "Echo response timestamps do not form valid durations";
                    return false;
                }
                outResult->applicationObservedRttNanoseconds.push_back(applicationObservedRttNanoseconds);
                outResult->serverProcessingDurationNanoseconds.push_back(serverProcessingDurationNanoseconds);
            }

            client.outstandingRequests.erase(requestIterator);
            ++outResult->completed;
            return true;
        }

        [[nodiscard]] bool TryDrainClientEvent(BenchmarkLoadClient& client,
                                               BenchmarkClientWorkloadResult* const outResult,
                                               bool* const outEventPopped, std::string* const outError)
        {
            psnr::runtime::NrClientEvent event;
            const std::string readError =
                BenchmarkClientTransport::TryReadNextEvent(client.client, &event, outEventPopped);
            if (!readError.empty())
            {
                *outError = readError;
                return false;
            }
            if (!*outEventPopped)
            {
                return true;
            }
            if (event.Kind() == psnr::runtime::NrClientEventKind::TransportDisconnected)
            {
                ++outResult->unexpectedDisconnect;
                *outError = "NrClient disconnected during fixed-rate workload";
                return false;
            }

            if (event.Kind() != psnr::runtime::NrClientEventKind::PacketReceived ||
                !CompleteResponse(client, event, outResult, outError))
            {
                if (outError->empty())
                {
                    *outError = "NrClient received an unexpected event during fixed-rate workload";
                }
                return false;
            }
            return true;
        }

        [[nodiscard]] bool DrainClientEvents(BenchmarkLoadClient& client,
                                             BenchmarkClientWorkloadResult* const outResult,
                                             std::string* const outError)
        {
            bool eventPopped = true;
            while (eventPopped)
            {
                if (!TryDrainClientEvent(client, outResult, &eventPopped, outError))
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] std::uint64_t OutstandingRequestCount(const std::vector<BenchmarkLoadClient>& clients,
                                                            const std::size_t clientBegin,
                                                            const std::size_t clientEnd) noexcept
        {
            std::uint64_t count = 0;
            for (std::size_t clientIndex = clientBegin; clientIndex < clientEnd; ++clientIndex)
            {
                count += static_cast<std::uint64_t>(clients[clientIndex].outstandingRequests.size());
            }
            return count;
        }

        [[nodiscard]] BenchmarkClientWorkloadResult RunLoadWorker(
            std::vector<BenchmarkLoadClient>& clients, const std::size_t clientBegin, const std::size_t clientEnd,
            const std::uint32_t workerIndex, const BenchmarkConfigV1& config,
            const BenchmarkWorkloadTimelineSnapshot& timeline, const std::chrono::nanoseconds globalInterval)
        {
            const std::uint64_t workerClientCount = static_cast<std::uint64_t>(clientEnd - clientBegin);
            const std::uint64_t workerRequestRate = workerClientCount * config.workload.requestRatePerClient;
            const std::int64_t workerIntervalNanoseconds =
                1'000'000'000LL / static_cast<std::int64_t>(workerRequestRate);
            const std::chrono::nanoseconds workerInterval{workerIntervalNanoseconds};
            std::chrono::steady_clock::time_point nextScheduledAt =
                timeline.workloadBegin + globalInterval * workerIndex;
            std::uint64_t localScheduleIndex = 0;
            std::size_t nextDrainClientIndex = clientBegin;
            BenchmarkClientWorkloadResult workerResult;

            while (std::chrono::steady_clock::now() < timeline.measurementEnd)
            {
                const std::chrono::steady_clock::time_point observedAt = std::chrono::steady_clock::now();
                if (observedAt < nextScheduledAt)
                {
                    std::this_thread::yield();
                    continue;
                }

                if (observedAt >= nextScheduledAt)
                {
                    const std::int64_t overdueNanoseconds =
                        std::chrono::duration_cast<std::chrono::nanoseconds>(observedAt - nextScheduledAt).count();
                    const std::int64_t missedIntervals = overdueNanoseconds / workerIntervalNanoseconds;
                    if (missedIntervals > 0) // missed 카운팅
                    {
                        workerResult.missedSchedule += static_cast<std::uint64_t>(missedIntervals);
                        localScheduleIndex += static_cast<std::uint64_t>(missedIntervals);
                        nextScheduledAt += workerInterval * missedIntervals; // missed 만큼 interval 추가
                    }

                    if (nextScheduledAt < timeline.measurementEnd) // benchmark 진행 가능한 시간
                    {
                        const std::size_t clientIndex =
                            clientBegin + static_cast<std::size_t>(localScheduleIndex % workerClientCount);
                        BenchmarkLoadClient& client = clients[clientIndex];
                        const std::uint64_t sequence = client.nextSequence;
                        const std::uint64_t clientSendTimestampNanoseconds = SteadyNanosecondsNow();
                        if (clientSendTimestampNanoseconds == 0)
                        {
                            return Failure(std::move(workerResult), "failed to capture client send timestamp");
                        }
                        const BenchmarkPayload requestFields{
                            BenchmarkProtocolVersion,
                            BenchmarkOperation::Echo,
                            client.clientId,
                            sequence,
                            clientSendTimestampNanoseconds,
                            0,
                            0,
                        };
                        const BenchmarkProtocolCodec::CanonicalPayload requestPayload =
                            BenchmarkProtocolCodec::EncodeCanonical(requestFields);
                        const psnr::runtime::NrByteView requestView{
                            requestPayload.data(),
                            static_cast<std::uint32_t>(requestPayload.size()),
                        };

                        // send packet
                        const psnr::core::NrStatus sendStatus =
                            client.client.Send(psnr::core::NrPacketType{BenchmarkRequestPacketType}, requestView);
                        if (sendStatus.Failed())
                        {
                            ++workerResult.sendRejected;
                            return Failure(std::move(workerResult),
                                           BenchmarkClientTransport::DescribeFailure("NrClient::Send", sendStatus));
                        }

                        const std::chrono::steady_clock::time_point submittedAt = std::chrono::steady_clock::now();
                        const OutstandingRequest request{
                            nextScheduledAt,
                            submittedAt,
                            clientSendTimestampNanoseconds,
                            nextScheduledAt >= timeline.measurementBegin,
                        };
                        const bool inserted = client.outstandingRequests.try_emplace(sequence, request).second;
                        if (!inserted)
                        {
                            return Failure(std::move(workerResult), "duplicate outstanding request sequence");
                        }

                        ++client.nextSequence;
                        ++localScheduleIndex;
                        nextScheduledAt += workerInterval;
                        ++workerResult.accepted;
                    }
                    else
                    {
                        break;
                    }
                }

                for (std::size_t probeCount = 0; probeCount < BenchmarkActiveDrainProbeBudget; ++probeCount)
                {
                    if (std::chrono::steady_clock::now() >= nextScheduledAt)
                    {
                        break;
                    }

                    std::string drainError;
                    bool eventPopped = false;
                    if (!TryDrainClientEvent(clients[nextDrainClientIndex], &workerResult, &eventPopped, &drainError))
                    {
                        return Failure(std::move(workerResult), std::move(drainError));
                    }
                    ++nextDrainClientIndex;
                    if (nextDrainClientIndex == clientEnd)
                    {
                        nextDrainClientIndex = clientBegin;
                    }
                }
            }

            // drain loop
            const std::chrono::steady_clock::time_point drainDeadline =
                std::chrono::steady_clock::now() + std::chrono::seconds(config.phases.drainTimeoutSeconds);
            while (OutstandingRequestCount(clients, clientBegin, clientEnd) != 0 &&
                   std::chrono::steady_clock::now() < drainDeadline)
            {
                for (std::size_t clientIndex = clientBegin; clientIndex < clientEnd; ++clientIndex)
                {
                    std::string drainError;
                    if (!DrainClientEvents(clients[clientIndex], &workerResult, &drainError))
                    {
                        return Failure(std::move(workerResult), std::move(drainError));
                    }
                }
                std::this_thread::sleep_for(EventPollInterval);
            }

            const std::uint64_t remainingRequestCount = OutstandingRequestCount(clients, clientBegin, clientEnd);
            if (remainingRequestCount != 0)
            {
                workerResult.timeout = remainingRequestCount;
                return Failure(std::move(workerResult), "fixed-rate workload drain timed out");
            }
            return workerResult;
        }

        void AccumulateWorkerResult(BenchmarkClientWorkloadResult&& workerResult, const std::uint32_t workerIndex,
                                    BenchmarkClientWorkloadResult* const outResult)
        {
            outResult->accepted += workerResult.accepted;
            outResult->completed += workerResult.completed;
            outResult->sendRejected += workerResult.sendRejected;
            outResult->timeout += workerResult.timeout;
            outResult->unexpectedDisconnect += workerResult.unexpectedDisconnect;
            outResult->missedSchedule += workerResult.missedSchedule;
            outResult->applicationObservedRttNanoseconds.insert(outResult->applicationObservedRttNanoseconds.end(),
                                                                workerResult.applicationObservedRttNanoseconds.begin(),
                                                                workerResult.applicationObservedRttNanoseconds.end());
            outResult->serverProcessingDurationNanoseconds.insert(
                outResult->serverProcessingDurationNanoseconds.end(),
                workerResult.serverProcessingDurationNanoseconds.begin(),
                workerResult.serverProcessingDurationNanoseconds.end());
            outResult->schedulerLagNanoseconds.insert(outResult->schedulerLagNanoseconds.end(),
                                                      workerResult.schedulerLagNanoseconds.begin(),
                                                      workerResult.schedulerLagNanoseconds.end());
            if (outResult->error.empty() && !workerResult.error.empty())
            {
                outResult->error = "load worker ";
                outResult->error.append(std::to_string(workerIndex));
                outResult->error.append(": ");
                outResult->error.append(workerResult.error);
            }
        }
    } // namespace

    void BenchmarkWorkloadTimeline::Publish(const BenchmarkWorkloadTimelineSnapshot& timeline)
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        timeline_ = timeline;
        published_ = true;
    }

    bool BenchmarkWorkloadTimeline::TryRead(BenchmarkWorkloadTimelineSnapshot* const outTimeline) const
    {
        if (outTimeline == nullptr)
        {
            return false;
        }

        const std::lock_guard<std::mutex> lock(mutex_);
        if (!published_)
        {
            return false;
        }

        *outTimeline = timeline_;
        return true;
    }

    BenchmarkClientWorkloadResult BenchmarkClientWorkload::Run(const BenchmarkConfigV1& config,
                                                               BenchmarkWorkloadTimeline& timeline)
    {
        // 1. 연결 준비: benchmark server endpoint와 config에 지정된 수의 public NrClient를 생성한다.
        psnr::runtime::NrEndpoint endpoint;
        if (!BenchmarkEndpointParser::TryParseServerEndpoint(config.server, &endpoint))
        {
            return Failure("server.address must be a dotted-decimal IPv4 address");
        }

        std::vector<BenchmarkLoadClient> clients;
        clients.reserve(config.connection.clientCount);
        for (std::uint32_t clientIndex = 0; clientIndex < config.connection.clientCount; ++clientIndex)
        {
            psnr::runtime::NrClient client;
            const psnr::core::NrStatus createStatus =
                psnr::runtime::NrClient::Create(psnr::runtime::NrClientConfig{}, &client);
            if (createStatus.Failed())
            {
                return Failure(BenchmarkClientTransport::DescribeFailure("NrClient::Create", createStatus));
            }
            clients.emplace_back(clientIndex + 1, std::move(client));
        }

        // 2. 연결 확인: batch 단위로 connect를 제출하고 batch 전체의 TransportConnected를 확인한다.
        const std::chrono::steady_clock::time_point connectDeadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(config.connection.timeoutSeconds);
        for (std::uint32_t batchBegin = 0; batchBegin < config.connection.clientCount;
             batchBegin += config.connection.batchSize)
        {
            const std::uint32_t remainingClientCount = config.connection.clientCount - batchBegin;
            const std::uint32_t batchClientCount = std::min(config.connection.batchSize, remainingClientCount);
            const std::uint32_t batchEnd = batchBegin + batchClientCount;
            for (std::uint32_t clientIndex = batchBegin; clientIndex < batchEnd; ++clientIndex)
            {
                const psnr::core::NrStatus connectStatus = clients[clientIndex].client.Connect(endpoint);
                if (connectStatus.Failed())
                {
                    return Failure(BenchmarkClientTransport::DescribeFailure("NrClient::Connect", connectStatus));
                }
            }

            for (std::uint32_t clientIndex = batchBegin; clientIndex < batchEnd; ++clientIndex)
            {
                psnr::runtime::NrClientEvent connectedEvent;
                std::string connectError;
                if (!WaitForEvent(clients[clientIndex].client, psnr::runtime::NrClientEventKind::TransportConnected,
                                  connectDeadline, &connectedEvent, &connectError))
                {
                    return Failure(std::move(connectError));
                }
            }
        }

        // 3. Fixed-rate 실행: 각 worker가 연속된 client group을 독점하고 local aggregate schedule을 실행한다.
        const std::uint64_t aggregateRequestRate =
            static_cast<std::uint64_t>(config.connection.clientCount) * config.workload.requestRatePerClient;
        if (aggregateRequestRate == 0 || aggregateRequestRate > 1'000'000'000ULL)
        {
            return Failure("aggregate request rate exceeds the supported nanosecond scheduler resolution");
        }
        const std::int64_t aggregateIntervalNanoseconds =
            1'000'000'000LL / static_cast<std::int64_t>(aggregateRequestRate);
        const std::chrono::nanoseconds aggregateInterval{aggregateIntervalNanoseconds};

        const std::uint32_t minimumClientsPerWorker = config.connection.clientCount / config.workload.workerCount;
        const std::uint32_t workersWithExtraClient = config.connection.clientCount % config.workload.workerCount;

        std::vector<BenchmarkClientWorkloadResult> workerResults(config.workload.workerCount);
        std::vector<std::thread> workers;
        workers.reserve(config.workload.workerCount);
        std::atomic<bool> startRequested = false;
        std::atomic<bool> cancelRequested = false;
        BenchmarkWorkloadTimelineSnapshot workloadTimeline;

        try
        {
            // load worker 생성
            for (std::uint32_t workerIndex = 0; workerIndex < config.workload.workerCount; ++workerIndex)
            {
                const std::uint32_t extraClientsBefore = std::min(workerIndex, workersWithExtraClient);
                const std::size_t clientBegin =
                    static_cast<std::size_t>(workerIndex) * minimumClientsPerWorker + extraClientsBefore;
                const std::uint32_t workerClientCount =
                    minimumClientsPerWorker + (workerIndex < workersWithExtraClient ? 1U : 0U);
                const std::size_t clientEnd = clientBegin + workerClientCount;
                workers.emplace_back(
                    [&, clientBegin, clientEnd, workerIndex]()
                    {
                        while (!startRequested.load(std::memory_order_acquire))
                        {
                            if (cancelRequested.load(std::memory_order_acquire))
                            {
                                return;
                            }
                            std::this_thread::yield();
                        }
                        if (cancelRequested.load(std::memory_order_acquire))
                        {
                            return;
                        }
                        workerResults[workerIndex] = RunLoadWorker(clients, clientBegin, clientEnd, workerIndex, config,
                                                                   workloadTimeline, aggregateInterval);
                    });
            }
        }
        catch (const std::exception& exception)
        {
            cancelRequested.store(true, std::memory_order_release);
            startRequested.store(true, std::memory_order_release);
            for (std::thread& worker : workers)
            {
                worker.join();
            }
            return Failure(std::string("failed to start load workers: ") + exception.what());
        }

        try
        {
            workloadTimeline.workloadBegin = std::chrono::steady_clock::now() + WorkerStartLeadTime;
            workloadTimeline.measurementBegin =
                workloadTimeline.workloadBegin + std::chrono::seconds(config.phases.warmupSeconds);
            workloadTimeline.measurementEnd =
                workloadTimeline.measurementBegin + std::chrono::seconds(config.phases.measurementSeconds);
            timeline.Publish(workloadTimeline);
            startRequested.store(true, std::memory_order_release); // start load workers
        }
        catch (const std::exception& exception)
        {
            cancelRequested.store(true, std::memory_order_release);
            startRequested.store(true, std::memory_order_release);
            for (std::thread& worker : workers)
            {
                worker.join();
            }
            return Failure(std::string("failed to publish workload timeline: ") + exception.what());
        }

        for (std::thread& worker : workers)
        {
            worker.join();
        }

        BenchmarkClientWorkloadResult workloadResult;
        for (std::uint32_t workerIndex = 0; workerIndex < config.workload.workerCount; ++workerIndex)
        {
            AccumulateWorkerResult(std::move(workerResults[workerIndex]), workerIndex, &workloadResult);
        }
        if (!workloadResult.Succeeded())
        {
            return workloadResult;
        }

        // 4. 연결 정리: 모든 client의 local disconnect event를 확인한 뒤 client runtime을 shutdown한다.
        for (BenchmarkLoadClient& client : clients)
        {
            const psnr::core::NrStatus disconnectStatus = client.client.Disconnect();
            if (disconnectStatus.Failed())
            {
                return Failure(std::move(workloadResult),
                               BenchmarkClientTransport::DescribeFailure("NrClient::Disconnect", disconnectStatus));
            }
        }

        const std::chrono::steady_clock::time_point disconnectDeadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(config.phases.drainTimeoutSeconds);
        for (BenchmarkLoadClient& client : clients)
        {
            psnr::runtime::NrClientEvent disconnectedEvent;
            std::string disconnectError;
            // 5-1. TransportDisconnected event 대기
            if (!WaitForEvent(client.client, psnr::runtime::NrClientEventKind::TransportDisconnected,
                              disconnectDeadline, &disconnectedEvent, &disconnectError))
            {
                return Failure(std::move(workloadResult), std::move(disconnectError));
            }
        }

        for (BenchmarkLoadClient& client : clients)
        {
            const psnr::core::NrStatus shutdownStatus =
                client.client.Shutdown(); // 5-2. event 확인 후 NrClient shutdown
            if (shutdownStatus.Failed())
            {
                return Failure(std::move(workloadResult),
                               BenchmarkClientTransport::DescribeFailure("NrClient::Shutdown", shutdownStatus));
            }
        }

        if (workloadResult.sendRejected != 0 || workloadResult.unexpectedDisconnect != 0 ||
            workloadResult.timeout != 0 || workloadResult.accepted != workloadResult.completed)
        {
            std::string error("fixed-rate workload validity check failed: accepted=");
            error.append(std::to_string(workloadResult.accepted));
            error.append(" completed=");
            error.append(std::to_string(workloadResult.completed));
            error.append(" missedSchedule=");
            error.append(std::to_string(workloadResult.missedSchedule));
            error.append(" sendRejected=");
            error.append(std::to_string(workloadResult.sendRejected));
            error.append(" timeout=");
            error.append(std::to_string(workloadResult.timeout));
            error.append(" unexpectedDisconnect=");
            error.append(std::to_string(workloadResult.unexpectedDisconnect));
            return Failure(std::move(workloadResult), std::move(error));
        }
        return workloadResult;
    }
} // namespace psnr::benchmark
