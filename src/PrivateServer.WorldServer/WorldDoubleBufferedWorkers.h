#pragma once

#include "NrServerWorldEventSource.h"
#include "IWorldTickSampleSink.h"
#include "WorldClock.h"
#include "WorldDoubleBufferedTickCoordinator.h"
#include "WorldIngressEventConsumer.h"
#include "WorldIngressPump.h"
#include "WorldIngressTerminalConsumer.h"
#include "WorldOutboundPublisher.h"

#include <PrivateServer/NetworkRuntime/NrGateway.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

/*
Worker 작동 프로세스
- ingress 정상 실행은 role exchange의 swap/claim이 협업 기준
- ingress plan/completion은 terminal drain에서만 사용

Ingress Pump
    -> Runtime event queue에서 현재 write slot으로 continuous drain

Coordinator
    -> tick deadline에 inbound role swap
    -> sealed inbound N 처리와 동시에 Pump는 다음 write slot 기록
    -> World Logic 실행
    -> Outbound buffer N seal

Outbound Publisher
    -> Ready FIFO에서 outbound N 직접 acquire/전송
    -> read buffer release


동기화 채널
1. WorldIngressWorkerExchange (exchange condvar)
    - terminal drain에서만 Coordinator -> Pump plan과 completion을 전달

2. WorldIngressDoubleBuffer
    - Pump write / Coordinator read ownership과 sealed data를 전달
    - buffer condvar는 자료구조 상태 보호용이며 worker 작업 시작 신호로 사용하지 않음

3. WorldOutboundDoubleBuffer
    - Coordinator write / Publisher read ownership과 sealed data를 전달
    - Ready FIFO와 buffer condvar가 publisher 작업 시작과 bounded backpressure를 담당
*/

namespace psnr::world
{
    struct WorldIngressWorkerPlan final
    {
        std::uint64_t epoch = 0;
        WorldClock::time_point deadline{};
    };

    enum class WorldIngressWorkerExchangeResult : std::uint8_t
    {
        Exchanged = 0,
        Closed,
        InvalidArgument,
        InvalidState,
    };

    struct WorldIngressWorkerCompletion final
    {
        std::uint64_t epoch = 0;
        WorldIngressTerminalEpochReport terminalReport{};
    };

    // terminal drain plan 하나를 게시하고 pump가 같은 epoch의 completion을 돌려주는 단일-slot handoff다.
    class WorldIngressWorkerExchange final
    {
    public:
        [[nodiscard]] WorldIngressWorkerExchangeResult Publish(const WorldIngressWorkerPlan& plan) noexcept;
        [[nodiscard]] WorldIngressWorkerExchangeResult WaitTakePlan(WorldIngressWorkerPlan* outPlan) noexcept;
        [[nodiscard]] WorldIngressWorkerExchangeResult Complete(
            const WorldIngressWorkerCompletion& completion) noexcept;
        [[nodiscard]] WorldIngressWorkerExchangeResult WaitTakeCompletion(
            std::uint64_t epoch, WorldIngressWorkerCompletion* outCompletion) noexcept;
        void Close() noexcept;

    private:
        std::mutex mutex_;
        std::condition_variable condition_;
        WorldIngressWorkerPlan plan_{};
        WorldIngressWorkerCompletion completion_{};
        bool planReady_ = false;
        bool completionReady_ = false;
        bool closed_ = false;
    };

    enum class WorldConcreteWorkerStopReason : std::uint8_t
    {
        NotStarted = 0,
        Running,
        StopRequested,
        Completed,
        OperationFailed,
        StartFailed,
    };

    class WorldOutboundPublisherWorker final
    {
    public:
        WorldOutboundPublisherWorker(WorldOutboundPublisher& publisher, psnr::runtime::NrGateway& gateway) noexcept;
        ~WorldOutboundPublisherWorker();

        WorldOutboundPublisherWorker(const WorldOutboundPublisherWorker&) = delete;
        WorldOutboundPublisherWorker& operator=(const WorldOutboundPublisherWorker&) = delete;

        [[nodiscard]] bool Start() noexcept;
        void RequestStop() noexcept;
        [[nodiscard]] bool DrainAndStop() noexcept;
        void Join() noexcept;

        [[nodiscard]] WorldConcreteWorkerStopReason StopReason() const noexcept;
        [[nodiscard]] WorldOutboundPublishReport LastReport() const noexcept;

    private:
        void Run() noexcept;

        WorldOutboundPublisher& publisher_;
        psnr::runtime::NrGateway& gateway_;
        std::thread thread_;
        std::atomic<bool> started_ = false;
        std::atomic<bool> stopRequested_ = false;
        std::atomic<bool> drainRequested_ = false;
        std::atomic<WorldConcreteWorkerStopReason> stopReason_ = WorldConcreteWorkerStopReason::NotStarted;
        mutable std::mutex reportMutex_;
        WorldOutboundPublishReport lastReport_{};
    };

    class WorldIngressPumpWorker final
    {
    public:
        WorldIngressPumpWorker(WorldIngressPump& pump, NrServerWorldEventSource& source,
                               WorldIngressWorkerExchange& exchange) noexcept;
        ~WorldIngressPumpWorker();

        WorldIngressPumpWorker(const WorldIngressPumpWorker&) = delete;
        WorldIngressPumpWorker& operator=(const WorldIngressPumpWorker&) = delete;

        [[nodiscard]] bool Start() noexcept;
        void RequestStop() noexcept;
        void EnterTerminalDrain() noexcept;
        void Join() noexcept;

        [[nodiscard]] WorldConcreteWorkerStopReason StopReason() const noexcept;
        [[nodiscard]] bool TerminalDrainSucceeded() const noexcept;

    private:
        void Run() noexcept;

        WorldIngressPump& pump_;
        NrServerWorldEventSource& source_;
        WorldIngressWorkerExchange& exchange_;
        WorldSteadyClockSource clockSource_;
        std::thread thread_;
        std::atomic<bool> started_ = false;
        std::atomic<bool> stopRequested_ = false;
        std::atomic<bool> terminalDrainEnabled_ = false;
        std::atomic<WorldConcreteWorkerStopReason> stopReason_ = WorldConcreteWorkerStopReason::NotStarted;
    };

    struct WorldDoubleBufferedCoordinatorWorkerConfig final
    {
        std::chrono::milliseconds maxShutdownDrainWait{};
    };

    class WorldDoubleBufferedCoordinatorWorker final
    {
    public:
        WorldDoubleBufferedCoordinatorWorker(const WorldDoubleBufferedCoordinatorWorkerConfig& config,
                                             WorldDoubleBufferedTickCoordinator& coordinator,
                                             WorldIngressDoubleBuffer& ingressBuffer,
                                             WorldIngressEventConsumer& eventConsumer,
                                             WorldIngressWorkerExchange& ingressExchange,
                                             std::unique_ptr<WorldTickSampleBuffer> tickSampleBuffer = nullptr,
                                             IWorldTickSampleSink* tickSampleSink = nullptr) noexcept;
        ~WorldDoubleBufferedCoordinatorWorker();

        WorldDoubleBufferedCoordinatorWorker(const WorldDoubleBufferedCoordinatorWorker&) = delete;
        WorldDoubleBufferedCoordinatorWorker& operator=(const WorldDoubleBufferedCoordinatorWorker&) = delete;

        [[nodiscard]] bool Start() noexcept;
        void RequestStop() noexcept;
        [[nodiscard]] bool StopGameplayAndWait() noexcept;
        void EnterTerminalConsume() noexcept;
        void Join() noexcept;

        [[nodiscard]] WorldConcreteWorkerStopReason StopReason() const noexcept;
        [[nodiscard]] bool TerminalConsumeSucceeded() const noexcept;
        [[nodiscard]] WorldDoubleBufferedTickReport LastTickReport() const noexcept;
        [[nodiscard]] WorldIngressTerminalConsumeReport TerminalConsumeReport() const noexcept;
        [[nodiscard]] std::uint64_t TickSampleCollectionFailureCount() const noexcept;

    private:
        void Run() noexcept;
        [[nodiscard]] bool RunTickLoop() noexcept;
        [[nodiscard]] bool WaitForTerminalMode() noexcept;
        [[nodiscard]] bool RunTerminalLoop() noexcept;
        [[nodiscard]] std::chrono::milliseconds RemainingTerminalWait() const noexcept;
        [[nodiscard]] bool PublishTickSamples(WorldTickSampleBatchCompleteness completeness) noexcept;
        void RotateCompletedRoundTickSamples() noexcept;

        WorldDoubleBufferedCoordinatorWorkerConfig config_{};
        WorldDoubleBufferedTickCoordinator& coordinator_;
        WorldIngressDoubleBuffer& ingressBuffer_;
        WorldIngressEventConsumer& eventConsumer_;
        WorldIngressWorkerExchange& ingressExchange_;
        std::unique_ptr<WorldTickSampleBuffer> tickSampleBuffer_;
        IWorldTickSampleSink* tickSampleSink_ = nullptr;
        WorldSteadyClockSource clockSource_;
        WorldIngressTerminalConsumer terminalConsumer_;
        std::thread thread_;
        std::atomic<bool> started_ = false;
        std::atomic<bool> stopRequested_ = false;
        std::atomic<bool> gameplayStopRequested_ = false;
        std::atomic<bool> gameplayStopped_ = false;
        std::atomic<bool> terminalConsumeEnabled_ = false;
        std::atomic<WorldConcreteWorkerStopReason> stopReason_ = WorldConcreteWorkerStopReason::NotStarted;
        std::atomic<std::uint64_t> tickSampleCollectionFailureCount_ = 0;
        std::mutex lifecycleMutex_;
        std::condition_variable lifecycleCondition_;
        WorldClock::time_point terminalDeadline_{};
        mutable std::mutex reportMutex_;
        WorldDoubleBufferedTickReport lastTickReport_{};
        WorldIngressTerminalConsumeReport terminalConsumeReport_{};
    };
} // namespace psnr::world
