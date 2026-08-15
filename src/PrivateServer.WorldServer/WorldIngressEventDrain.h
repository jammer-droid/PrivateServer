#pragma once

#include <PrivateServer/NetworkRuntime/NrErrorCode.h>
#include <PrivateServer/NetworkRuntime/NrStatus.h>
#include <PrivateServer/NetworkRuntime/NrToWorldEvent.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace psnr::world
{
    enum class WorldIngressDrainStopReason : std::uint8_t
    {
        QueueEmpty = 0,
        BudgetExhausted,
        SourceFailure,
    };

    struct WorldIngressDrainReport final
    {
        WorldIngressDrainStopReason stopReason = WorldIngressDrainStopReason::QueueEmpty;
        std::size_t drainedEventCount = 0;
        psnr::core::NrStatus sourceStatus{};
    };

    // 한 번의 World 실행 기회에서 처리할 ToWorld event 수를 제한한다.
    // consumer는 Handle 호출 안에서 payload를 decode하거나 owned World 값으로 전환해야 한다.
    // Handle이 반환된 뒤에는 event와 event가 소유한 payload view를 보관하거나 사용하지 않는다.
    class WorldIngressEventDrain final
    {
    public:
        static constexpr std::size_t EventBatchCapacity = psnr::runtime::NrMaxToWorldEventBatchSize;

        template <typename TEventSource, typename TEventConsumer>
        [[nodiscard]] static WorldIngressDrainReport Drain(TEventSource& source, TEventConsumer& consumer,
                                                           const std::size_t maxEventCount)
        {
            if (maxEventCount == 0)
            {
                return WorldIngressDrainReport{
                    WorldIngressDrainStopReason::BudgetExhausted,
                    0,
                    psnr::core::NrStatus::Success(),
                };
            }

            std::array<psnr::runtime::NrToWorldEvent, EventBatchCapacity> eventBuffer;
            std::size_t drainedEventCount = 0;
            while (drainedEventCount < maxEventCount)
            {
                const std::size_t remainingEventCount = maxEventCount - drainedEventCount;
                const std::size_t requestedEventCount =
                    remainingEventCount < eventBuffer.size() ? remainingEventCount : eventBuffer.size();
                std::size_t eventCount = 0;
                const psnr::core::NrStatus popStatus =
                    source.TryPopBatch(eventBuffer.data(), requestedEventCount, &eventCount);
                if (popStatus.Failed())
                {
                    const WorldIngressDrainStopReason stopReason =
                        popStatus.ErrorCode() == psnr::core::NrErrorCode::QueueEmpty
                            ? WorldIngressDrainStopReason::QueueEmpty
                            : WorldIngressDrainStopReason::SourceFailure;
                    return WorldIngressDrainReport{stopReason, drainedEventCount, popStatus};
                }

                for (std::size_t index = 0; index < eventCount; ++index)
                {
                    consumer.Handle(eventBuffer[index]);
                    eventBuffer[index] = psnr::runtime::NrToWorldEvent{};
                }
                drainedEventCount += eventCount;

                if (eventCount < requestedEventCount)
                {
                    return WorldIngressDrainReport{
                        WorldIngressDrainStopReason::QueueEmpty,
                        drainedEventCount,
                        psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::QueueEmpty),
                    };
                }
            }

            return WorldIngressDrainReport{
                WorldIngressDrainStopReason::BudgetExhausted,
                drainedEventCount,
                psnr::core::NrStatus::Success(),
            };
        }
    };
} // namespace psnr::world
