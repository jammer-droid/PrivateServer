#pragma once

#include "ApplicationLogSeverity.h"
#include "WorldIngressEventConsumer.h"
#include "WorldOutboundDoubleBuffer.h"
#include "WorldResult.h"
#include "WorldTickProcessor.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace psnr::world::host
{
    struct WorldServerHostChannelConfig final
    {
        static constexpr std::size_t MaximumNameBytes = 64;

        std::uint32_t id = 0;
        std::string name;
    };

    struct WorldServerHostPayloadPoolConfig final
    {
        std::uint32_t payload64BlockCount = 1024;
        std::uint32_t payload256BlockCount = 1024;
        std::uint32_t payload1024BlockCount = 1024;
        std::uint32_t payload8192BlockCount = 1024;
        std::uint32_t payloadRefControlBlockCount = 1024;
    };

    struct WorldServerHostNetworkConfig final
    {
        std::array<std::uint8_t, 4> bindAddress{127, 0, 0, 1}; // IPv4 only
        std::uint16_t port = 0;
        std::int32_t listenBacklog = 0;    // TCP hanshake 완료 이후, OS Accept 대기열 최대치
        std::uint32_t acceptSlotCount = 0; // 최대 AcceptEx 요청 수
        std::uint32_t actorMailboxCapacity = 0;
        std::uint32_t pendingSendQueueCapacity = 0;
        std::uint32_t maxSessionCount = 0; // 서버 수용 최대 활성 세션 수
        std::uint32_t toWorldEventCapacity = 0;
        WorldServerHostPayloadPoolConfig payloadPools{};
    };

    struct WorldServerHostExecutionConfig final
    {
        std::uint32_t tickRateHz = 0;      // 1초에 실행할 world tick
        std::uint32_t maxCatchUpSteps = 0; // tick 처리가 밀렸을 때, 한 번에 따라잡을 수 있는 최대 tick
        std::uint32_t inboundEventCapacityPerSlot = 0;
        WorldOutboundBatchCapacity outboundCapacityPerSlot{};
        std::uint32_t shutdownDrainTimeoutMilliseconds = 0;
        std::uint32_t tickSampleCapacity = 12000;
        std::uint32_t tickSampleWriteQueueCapacity = 8;
        std::uint32_t runtimeSampleIntervalMilliseconds = 1000;
        std::uint32_t runtimeSampleWriteQueueCapacity = 8;
    };

    struct WorldServerHostConfig final
    {
        WorldServerHostChannelConfig channel{};
        psnr::logging::ApplicationLogSeverity minimumLogSeverity = psnr::logging::ApplicationLogSeverity::Info;
        WorldServerHostNetworkConfig network{};
        WorldServerHostExecutionConfig execution{};
        WorldTickProcessorConfig tickProcessor{};
        WorldIngressEventConsumerConfig consumer{};
    };

    class WorldServerHostConfigSource final
    {
    public:
        [[nodiscard]] static WorldResult<WorldServerHostConfig, std::string> Load(const std::filesystem::path& path);
        [[nodiscard]] static std::string Validate(const WorldServerHostConfig& config);
        [[nodiscard]] static WorldResult<std::string, std::string> SerializeNormalized(
            const WorldServerHostConfig& config);
        [[nodiscard]] static WorldResult<void, std::string> WriteNormalized(const std::filesystem::path& path,
                                                                            std::string_view normalizedJson);
    };
} // namespace psnr::world::host
