#include "pch.h"

#include "WorldServerHostConfig.h"

#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

namespace psnr::world::tests
{
    namespace
    {
        constexpr std::string_view ValidConfigJson = R"json({
  "schema": "psnr.world_server.host.config",
  "version": 2,
  "channel": { "id": 7, "name": "Channel 7" },
  "logging": { "minimumSeverity": "info" },
  "network": {
    "bindAddress": "127.0.0.1",
    "port": 27015,
    "listenBacklog": 64,
    "acceptSlotCount": 1,
    "actorMailboxCapacity": 64,
    "pendingSendQueueCapacity": 1024,
    "maxSessionCount": 100,
    "toWorldEventCapacity": 128,
    "payloadPools": {
      "payload64BlockCount": 4096,
      "payload256BlockCount": 1024,
      "payload1024BlockCount": 1024,
      "payload8192BlockCount": 1024,
      "payloadRefControlBlockCount": 4096
    }
  },
  "execution": {
    "tickRateHz": 60,
    "maxCatchUpSteps": 4,
    "inboundEventCapacityPerSlot": 128,
    "outboundRecordCapacityPerSlot": 256,
    "outboundRecipientCapacityPerSlot": 1024,
    "outboundPayloadByteCapacityPerSlot": 65536,
    "shutdownDrainTimeoutMilliseconds": 2000,
    "tickSampleCapacity": 12000,
    "tickSampleWriteQueueCapacity": 8,
    "runtimeSampleIntervalMilliseconds": 1000,
    "runtimeSampleWriteQueueCapacity": 8
  },
  "simulation": {
    "physics": { "maxContactsPerTick": 4, "collisionEpsilon": 0.0001, "contactTolerance": 0.0001 },
    "arena": { "minimumX": -100.0, "minimumY": -100.0, "maximumX": 100.0, "maximumY": 100.0 },
    "movement": { "baseMoveSpeed": 5.0, "boostMoveSpeed": 7.5, "angularSpeedRadiansPerSecond": 3.141592653589793 },
    "growth": { "initialLength": 10.0, "lengthPerPoint": 0.25, "initialDiameter": 1.0, "diameterPerPoint": 0.01 },
    "body": { "sampleIntervalTicks": 3, "maxSampleCount": 560 },
    "player": { "archetypeId": 1, "circleRadius": 0.5, "spawnX": 0.0, "spawnY": 0.0, "spawnMaxCandidatesPerTick": 4 }
  },
  "replication": {
    "snapshotIntervalTicks": 2,
    "commandSlackTicks": 2,
    "spatialCellSize": 10.0,
    "aoiEnterRadius": 30.0,
    "aoiRetainRadius": 32.0,
    "replicationIntervalTicks": 2,
    "firstPlayerId": 1
  },
  "gameplay": {
    "minimumPlayersToStart": 2,
    "scoreToWin": 5,
    "roundDurationTicks": 10800,
    "endedDurationTicks": 180,
    "resourceArchetypeId": 2,
    "resourceCircleRadius": 0.25,
    "resourceScoreValue": 1,
    "resourceDensityPerUnit2": 0.05,
    "boostInitialLengthCostPerSecond": 4.0,
    "activeAreaStartRatio": 1.0,
    "activeAreaEndRatio": 0.25
  }
})json";

        class TemporaryConfigFile final
        {
        public:
            explicit TemporaryConfigFile(const std::string_view contents)
            {
                path_ = std::filesystem::temp_directory_path() /
                        ("private-server-world-host-config-" + std::to_string(GetCurrentProcessId()) + ".json");
                std::ofstream output(path_, std::ios::binary | std::ios::trunc);
                output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
                output.flush();
                valid_ = output.good();
            }

            ~TemporaryConfigFile()
            {
                std::error_code error;
                static_cast<void>(std::filesystem::remove(path_, error));
            }

            TemporaryConfigFile(const TemporaryConfigFile&) = delete;
            TemporaryConfigFile& operator=(const TemporaryConfigFile&) = delete;

            [[nodiscard]] bool IsValid() const noexcept
            {
                return valid_;
            }

            [[nodiscard]] const std::filesystem::path& Path() const noexcept
            {
                return path_;
            }

        private:
            std::filesystem::path path_;
            bool valid_ = false;
        };
    } // namespace

    TEST(WorldServerHostConfigTests, LoadsValidatedConfigAndSerializesEffectiveValues)
    {
        const TemporaryConfigFile file(ValidConfigJson);
        ASSERT_TRUE(file.IsValid());

        const WorldResult<host::WorldServerHostConfig, std::string> result =
            host::WorldServerHostConfigSource::Load(file.Path());

        ASSERT_TRUE(result.Succeeded()) << result.Error();
        EXPECT_EQ(result.Value().channel.id, static_cast<std::uint32_t>(7));
        EXPECT_EQ(result.Value().channel.name, "Channel 7");
        EXPECT_EQ(result.Value().consumer.join.channelId, static_cast<std::uint32_t>(7));
        EXPECT_EQ(result.Value().network.port, static_cast<std::uint16_t>(27015));
        EXPECT_EQ(result.Value().network.maxSessionCount, static_cast<std::uint32_t>(100));
        EXPECT_EQ(result.Value().network.payloadPools.payload64BlockCount, static_cast<std::uint32_t>(4096));
        EXPECT_EQ(result.Value().network.payloadPools.payloadRefControlBlockCount, static_cast<std::uint32_t>(4096));
        EXPECT_EQ(result.Value().execution.tickRateHz, static_cast<std::uint32_t>(60));
        EXPECT_EQ(result.Value().execution.inboundEventCapacityPerSlot, static_cast<std::uint32_t>(128));
        EXPECT_EQ(result.Value().execution.tickSampleCapacity, static_cast<std::uint32_t>(12000));
        EXPECT_EQ(result.Value().execution.tickSampleWriteQueueCapacity, static_cast<std::uint32_t>(8));
        EXPECT_EQ(result.Value().execution.runtimeSampleIntervalMilliseconds, static_cast<std::uint32_t>(1000));
        EXPECT_EQ(result.Value().execution.runtimeSampleWriteQueueCapacity, static_cast<std::uint32_t>(8));
        EXPECT_EQ(result.Value().consumer.gameplay.roundDurationTicks, static_cast<std::uint64_t>(10800));

        const WorldResult<std::string, std::string> normalizedResult =
            host::WorldServerHostConfigSource::SerializeNormalized(result.Value());
        ASSERT_TRUE(normalizedResult.Succeeded()) << normalizedResult.Error();
        EXPECT_NE(normalizedResult.Value().find("psnr.world_server.host.config"), std::string::npos);
        EXPECT_NE(normalizedResult.Value().find("\"channel\":{\"id\":7,\"name\":\"Channel 7\"}"),
                  std::string::npos);
        EXPECT_NE(normalizedResult.Value().find("\"maxSessionCount\":100"), std::string::npos);
        EXPECT_NE(normalizedResult.Value().find("\"payload64BlockCount\":4096"), std::string::npos);
        EXPECT_NE(normalizedResult.Value().find("\"payloadRefControlBlockCount\":4096"), std::string::npos);
        EXPECT_NE(normalizedResult.Value().find("\"tickSampleCapacity\":12000"), std::string::npos);
        EXPECT_NE(normalizedResult.Value().find("\"tickSampleWriteQueueCapacity\":8"), std::string::npos);
        EXPECT_NE(normalizedResult.Value().find("\"runtimeSampleIntervalMilliseconds\":1000"), std::string::npos);
        EXPECT_NE(normalizedResult.Value().find("\"runtimeSampleWriteQueueCapacity\":8"), std::string::npos);

        const WorldResult<void, std::string> writeResult =
            host::WorldServerHostConfigSource::WriteNormalized(file.Path(), normalizedResult.Value());
        ASSERT_TRUE(writeResult.Succeeded()) << writeResult.Error();
        std::ifstream effectiveConfig(file.Path(), std::ios::binary);
        ASSERT_TRUE(effectiveConfig.is_open());
        std::ostringstream effectiveConfigText;
        effectiveConfigText << effectiveConfig.rdbuf();
        EXPECT_EQ(effectiveConfigText.str(), normalizedResult.Value() + "\n");
    }

    TEST(WorldServerHostConfigTests, RejectsZeroChannelId)
    {
        std::string invalidJson(ValidConfigJson);
        const std::string expected = "\"id\": 7";
        const std::size_t idOffset = invalidJson.find(expected);
        ASSERT_NE(idOffset, std::string::npos);
        invalidJson.replace(idOffset, expected.size(), "\"id\": 0");
        const TemporaryConfigFile file(invalidJson);
        ASSERT_TRUE(file.IsValid());

        const WorldResult<host::WorldServerHostConfig, std::string> result =
            host::WorldServerHostConfigSource::Load(file.Path());

        EXPECT_FALSE(result.Succeeded());
        EXPECT_EQ(result.Error(), "channel id and name must be valid");
    }

    TEST(WorldServerHostConfigTests, RejectsBlankChannelName)
    {
        std::string invalidJson(ValidConfigJson);
        const std::string expected = "\"name\": \"Channel 7\"";
        const std::size_t nameOffset = invalidJson.find(expected);
        ASSERT_NE(nameOffset, std::string::npos);
        invalidJson.replace(nameOffset, expected.size(), "\"name\": \"   \"");
        const TemporaryConfigFile file(invalidJson);
        ASSERT_TRUE(file.IsValid());

        const WorldResult<host::WorldServerHostConfig, std::string> result =
            host::WorldServerHostConfigSource::Load(file.Path());

        EXPECT_FALSE(result.Succeeded());
        EXPECT_EQ(result.Error(), "channel id and name must be valid");
    }

    TEST(WorldServerHostConfigTests, RejectsUnknownFields)
    {
        std::string invalidJson(ValidConfigJson);
        const std::size_t networkEnd = invalidJson.find("\n  },\n  \"execution\"");
        ASSERT_NE(networkEnd, std::string::npos);
        invalidJson.insert(networkEnd, ",\n    \"unexpectedCapacity\": 1");
        const TemporaryConfigFile file(invalidJson);
        ASSERT_TRUE(file.IsValid());

        const WorldResult<host::WorldServerHostConfig, std::string> result =
            host::WorldServerHostConfigSource::Load(file.Path());

        EXPECT_FALSE(result.Succeeded());
        EXPECT_EQ(result.Error(), "network has missing or unknown fields");
    }

    TEST(WorldServerHostConfigTests, RejectsLegacyExecutionModeFields)
    {
        std::string invalidJson(ValidConfigJson);
        const std::string expected = "\"maxCatchUpSteps\": 4,";
        const std::size_t catchUpOffset = invalidJson.find(expected);
        ASSERT_NE(catchUpOffset, std::string::npos);
        invalidJson.insert(catchUpOffset + expected.size(), "\n    \"inboundMode\": \"targetServerTick\",");
        const TemporaryConfigFile file(invalidJson);
        ASSERT_TRUE(file.IsValid());

        const WorldResult<host::WorldServerHostConfig, std::string> result =
            host::WorldServerHostConfigSource::Load(file.Path());

        EXPECT_FALSE(result.Succeeded());
        EXPECT_EQ(result.Error(), "execution has missing or unknown fields");
    }

    TEST(WorldServerHostConfigTests, RejectsZeroPayloadPoolBlockCount)
    {
        std::string invalidJson(ValidConfigJson);
        const std::string expected = "\"payloadRefControlBlockCount\": 4096";
        const std::size_t capacityOffset = invalidJson.find(expected);
        ASSERT_NE(capacityOffset, std::string::npos);
        invalidJson.replace(capacityOffset, expected.size(), "\"payloadRefControlBlockCount\": 0");
        const TemporaryConfigFile file(invalidJson);
        ASSERT_TRUE(file.IsValid());

        const WorldResult<host::WorldServerHostConfig, std::string> result =
            host::WorldServerHostConfigSource::Load(file.Path());

        EXPECT_FALSE(result.Succeeded());
        EXPECT_EQ(result.Error(), "network capacities must be greater than zero");
    }

    TEST(WorldServerHostConfigTests, RejectsFractionalIntegerFields)
    {
        std::string invalidJson(ValidConfigJson);
        const std::string expected = "\"tickRateHz\": 60";
        const std::size_t tickRateOffset = invalidJson.find(expected);
        ASSERT_NE(tickRateOffset, std::string::npos);
        invalidJson.replace(tickRateOffset, expected.size(), "\"tickRateHz\": 59.5");
        const TemporaryConfigFile file(invalidJson);
        ASSERT_TRUE(file.IsValid());

        const WorldResult<host::WorldServerHostConfig, std::string> result =
            host::WorldServerHostConfigSource::Load(file.Path());

        EXPECT_FALSE(result.Succeeded());
        EXPECT_EQ(result.Error(), "execution.tickRateHz must be an integer");
    }

    TEST(WorldServerHostConfigTests, RejectsZeroTickSampleCapacity)
    {
        std::string invalidJson(ValidConfigJson);
        const std::string expected = "\"tickSampleCapacity\": 12000";
        const std::size_t capacityOffset = invalidJson.find(expected);
        ASSERT_NE(capacityOffset, std::string::npos);
        invalidJson.replace(capacityOffset, expected.size(), "\"tickSampleCapacity\": 0");
        const TemporaryConfigFile file(invalidJson);
        ASSERT_TRUE(file.IsValid());

        const WorldResult<host::WorldServerHostConfig, std::string> result =
            host::WorldServerHostConfigSource::Load(file.Path());

        EXPECT_FALSE(result.Succeeded());
        EXPECT_EQ(result.Error(), "execution schedule, capacities, and timeout must be valid");
    }

    TEST(WorldServerHostConfigTests, RejectsZeroTickSampleWriteQueueCapacity)
    {
        std::string invalidJson(ValidConfigJson);
        const std::string expected = "\"tickSampleWriteQueueCapacity\": 8";
        const std::size_t capacityOffset = invalidJson.find(expected);
        ASSERT_NE(capacityOffset, std::string::npos);
        invalidJson.replace(capacityOffset, expected.size(), "\"tickSampleWriteQueueCapacity\": 0");
        const TemporaryConfigFile file(invalidJson);
        ASSERT_TRUE(file.IsValid());

        const WorldResult<host::WorldServerHostConfig, std::string> result =
            host::WorldServerHostConfigSource::Load(file.Path());

        EXPECT_FALSE(result.Succeeded());
        EXPECT_EQ(result.Error(), "execution schedule, capacities, and timeout must be valid");
    }

    TEST(WorldServerHostConfigTests, RejectsZeroRuntimeSampleInterval)
    {
        std::string invalidJson(ValidConfigJson);
        const std::string expected = "\"runtimeSampleIntervalMilliseconds\": 1000";
        const std::size_t intervalOffset = invalidJson.find(expected);
        ASSERT_NE(intervalOffset, std::string::npos);
        invalidJson.replace(intervalOffset, expected.size(), "\"runtimeSampleIntervalMilliseconds\": 0");
        const TemporaryConfigFile file(invalidJson);
        ASSERT_TRUE(file.IsValid());

        const WorldResult<host::WorldServerHostConfig, std::string> result =
            host::WorldServerHostConfigSource::Load(file.Path());

        EXPECT_FALSE(result.Succeeded());
        EXPECT_EQ(result.Error(), "execution schedule, capacities, and timeout must be valid");
    }

    TEST(WorldServerHostConfigTests, RejectsZeroRuntimeSampleWriteQueueCapacity)
    {
        std::string invalidJson(ValidConfigJson);
        const std::string expected = "\"runtimeSampleWriteQueueCapacity\": 8";
        const std::size_t capacityOffset = invalidJson.find(expected);
        ASSERT_NE(capacityOffset, std::string::npos);
        invalidJson.replace(capacityOffset, expected.size(), "\"runtimeSampleWriteQueueCapacity\": 0");
        const TemporaryConfigFile file(invalidJson);
        ASSERT_TRUE(file.IsValid());

        const WorldResult<host::WorldServerHostConfig, std::string> result =
            host::WorldServerHostConfigSource::Load(file.Path());

        EXPECT_FALSE(result.Succeeded());
        EXPECT_EQ(result.Error(), "execution schedule, capacities, and timeout must be valid");
    }

    TEST(WorldServerHostConfigTests, RejectsPlayerRadiusThatConflictsWithGrowthDiameter)
    {
        std::string invalidJson(ValidConfigJson);
        const std::string expected = "\"circleRadius\": 0.5";
        const std::size_t radiusOffset = invalidJson.find(expected);
        ASSERT_NE(radiusOffset, std::string::npos);
        invalidJson.replace(radiusOffset, expected.size(), "\"circleRadius\": 0.75");
        const TemporaryConfigFile file(invalidJson);
        ASSERT_TRUE(file.IsValid());

        const WorldResult<host::WorldServerHostConfig, std::string> result =
            host::WorldServerHostConfigSource::Load(file.Path());

        EXPECT_FALSE(result.Succeeded());
        EXPECT_EQ(result.Error(), "join config is inconsistent with execution or player config");
    }
} // namespace psnr::world::tests
