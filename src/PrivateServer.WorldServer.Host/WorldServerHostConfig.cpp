#include "WorldServerHostConfig.h"

#include "WorldControlMovementSolver.h"
#include "WorldGameplayConfig.h"
#include "WorldPhysicsValues.h"
#include "WorldPlayerBody.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>

namespace psnr::world::host
{
    namespace
    {
        using JsonObject = nlohmann::ordered_json;

        constexpr std::string_view ConfigSchema = "psnr.world_server.host.config";
        constexpr std::uint32_t ConfigVersion = 2;

        [[nodiscard]] std::string_view LogLevelName(const psnr::logging::ApplicationLogSeverity level) noexcept
        {
            switch (level)
            {
            case psnr::logging::ApplicationLogSeverity::Debug:
                return "debug";
            case psnr::logging::ApplicationLogSeverity::Info:
                return "info";
            case psnr::logging::ApplicationLogSeverity::Warning:
                return "warning";
            case psnr::logging::ApplicationLogSeverity::Error:
                return "error";
            default:
                return {};
            }
        }

        [[nodiscard]] bool TryParseLogLevel(const std::string_view value,
                                            psnr::logging::ApplicationLogSeverity* const outLevel) noexcept
        {
            if (outLevel == nullptr)
            {
                return false;
            }
            if (value == "debug")
            {
                *outLevel = psnr::logging::ApplicationLogSeverity::Debug;
                return true;
            }
            if (value == "info")
            {
                *outLevel = psnr::logging::ApplicationLogSeverity::Info;
                return true;
            }
            if (value == "warning")
            {
                *outLevel = psnr::logging::ApplicationLogSeverity::Warning;
                return true;
            }
            if (value == "error")
            {
                *outLevel = psnr::logging::ApplicationLogSeverity::Error;
                return true;
            }
            return false;
        }

        [[nodiscard]] bool IsObjectWithKeys(const JsonObject& value, const std::set<std::string>& expectedKeys,
                                            const std::string_view path, std::string* const outError)
        {
            if (!value.is_object())
            {
                *outError = std::string(path) + " must be an object";
                return false;
            }

            std::set<std::string> actualKeys;
            for (JsonObject::const_iterator iterator = value.begin(); iterator != value.end(); ++iterator)
            {
                actualKeys.insert(iterator.key());
            }
            if (actualKeys != expectedKeys)
            {
                *outError = std::string(path) + " has missing or unknown fields";
                return false;
            }
            return true;
        }

        [[nodiscard]] const JsonObject* ReadObject(const JsonObject& parent, const std::string_view key,
                                                   const std::set<std::string>& expectedKeys,
                                                   const std::string_view path, std::string* const outError)
        {
            const JsonObject::const_iterator iterator = parent.find(key);
            if (iterator == parent.end() || !IsObjectWithKeys(*iterator, expectedKeys, path, outError))
            {
                if (iterator == parent.end())
                {
                    *outError = std::string(path) + " is required";
                }
                return nullptr;
            }
            return &(*iterator);
        }

        template <typename T>
        [[nodiscard]] bool ReadNumber(const JsonObject& parent, const std::string_view key, const std::string_view path,
                                      T* const outValue, std::string* const outError)
        {
            const JsonObject::const_iterator iterator = parent.find(key);
            if (iterator == parent.end())
            {
                *outError = std::string(path) + " must be numeric";
                return false;
            }

            if constexpr (std::is_integral_v<T>)
            {
                if (!iterator->is_number_integer() && !iterator->is_number_unsigned())
                {
                    *outError = std::string(path) + " must be an integer";
                    return false;
                }

                if (iterator->is_number_unsigned())
                {
                    const std::uint64_t value = iterator->get<std::uint64_t>();
                    if (value > static_cast<std::uint64_t>(std::numeric_limits<T>::max()))
                    {
                        *outError = std::string(path) + " is outside the supported range";
                        return false;
                    }
                    *outValue = static_cast<T>(value);
                    return true;
                }

                const std::int64_t value = iterator->get<std::int64_t>();
                if constexpr (std::is_unsigned_v<T>)
                {
                    if (value < 0 ||
                        static_cast<std::uint64_t>(value) > static_cast<std::uint64_t>(std::numeric_limits<T>::max()))
                    {
                        *outError = std::string(path) + " is outside the supported range";
                        return false;
                    }
                }
                else if (value < static_cast<std::int64_t>(std::numeric_limits<T>::min()) ||
                         value > static_cast<std::int64_t>(std::numeric_limits<T>::max()))
                {
                    *outError = std::string(path) + " is outside the supported range";
                    return false;
                }
                *outValue = static_cast<T>(value);
                return true;
            }
            else
            {
                if (!iterator->is_number())
                {
                    *outError = std::string(path) + " must be numeric";
                    return false;
                }
                const double value = iterator->get<double>();
                if (!std::isfinite(value) || value < static_cast<double>(std::numeric_limits<T>::lowest()) ||
                    value > static_cast<double>(std::numeric_limits<T>::max()))
                {
                    *outError = std::string(path) + " is outside the supported range";
                    return false;
                }
                *outValue = static_cast<T>(value);
                return true;
            }
        }

        [[nodiscard]] bool ReadString(const JsonObject& parent, const std::string_view key, const std::string_view path,
                                      std::string* const outValue, std::string* const outError)
        {
            const JsonObject::const_iterator iterator = parent.find(key);
            if (iterator == parent.end() || !iterator->is_string())
            {
                *outError = std::string(path) + " must be a string";
                return false;
            }
            *outValue = iterator->get<std::string>();
            return true;
        }

        [[nodiscard]] bool TryParseIpv4(const std::string& text, std::array<std::uint8_t, 4>* const outAddress) noexcept
        {
            if (outAddress == nullptr)
            {
                return false;
            }

            std::array<std::uint8_t, 4> address{};
            std::size_t begin = 0;
            for (std::size_t index = 0; index < address.size(); ++index)
            {
                const std::size_t end = index + 1 == address.size() ? text.size() : text.find('.', begin);
                if (end == std::string::npos || end == begin || end - begin > 3)
                {
                    return false;
                }

                std::uint32_t octet = 0;
                for (std::size_t characterIndex = begin; characterIndex < end; ++characterIndex)
                {
                    const char character = text[characterIndex];
                    if (character < '0' || character > '9')
                    {
                        return false;
                    }
                    octet = octet * 10 + static_cast<std::uint32_t>(character - '0');
                    if (octet > 255)
                    {
                        return false;
                    }
                }
                address[index] = static_cast<std::uint8_t>(octet);
                begin = end + 1;
            }
            if (begin != text.size() + 1)
            {
                return false;
            }

            *outAddress = address;
            return true;
        }

        [[nodiscard]] std::string Ipv4Text(const std::array<std::uint8_t, 4>& address)
        {
            return std::to_string(address[0]) + "." + std::to_string(address[1]) + "." + std::to_string(address[2]) +
                   "." + std::to_string(address[3]);
        }

        [[nodiscard]] bool ParseDocument(const JsonObject& document, WorldServerHostConfig* const outConfig,
                                         std::string* const outError)
        {
            if (!IsObjectWithKeys(
                    document,
                    {"schema", "version", "channel", "logging", "network", "execution", "simulation", "replication",
                     "gameplay"},
                    "config", outError))
            {
                return false;
            }

            std::string schema;
            std::uint32_t version = 0;
            if (!ReadString(document, "schema", "schema", &schema, outError) ||
                !ReadNumber(document, "version", "version", &version, outError) || schema != ConfigSchema ||
                version != ConfigVersion)
            {
                if (outError->empty())
                {
                    *outError = "unsupported config schema or version";
                }
                return false;
            }

            const JsonObject* const channel = ReadObject(document, "channel", {"id", "name"}, "channel", outError);
            const JsonObject* const logging = ReadObject(document, "logging", {"minimumSeverity"}, "logging", outError);
            const JsonObject* const network =
                ReadObject(document, "network",
                           {"bindAddress", "port", "listenBacklog", "acceptSlotCount", "actorMailboxCapacity",
                            "pendingSendQueueCapacity", "maxSessionCount", "toWorldEventCapacity", "payloadPools"},
                           "network", outError);
            const JsonObject* const execution = ReadObject(
                document, "execution",
                {"tickRateHz", "maxCatchUpSteps", "inboundEventCapacityPerSlot", "outboundRecordCapacityPerSlot",
                 "outboundRecipientCapacityPerSlot", "outboundPayloadByteCapacityPerSlot",
                 "shutdownDrainTimeoutMilliseconds", "tickSampleCapacity", "tickSampleWriteQueueCapacity",
                 "runtimeSampleIntervalMilliseconds", "runtimeSampleWriteQueueCapacity"},
                "execution", outError);
            const JsonObject* const simulation =
                ReadObject(document, "simulation", {"physics", "arena", "movement", "growth", "body", "player"},
                           "simulation", outError);
            const JsonObject* const replication =
                ReadObject(document, "replication",
                           {"snapshotIntervalTicks", "commandSlackTicks", "spatialCellSize", "aoiEnterRadius",
                            "aoiRetainRadius", "replicationIntervalTicks", "firstPlayerId"},
                           "replication", outError);
            const JsonObject* const gameplay = ReadObject(
                document, "gameplay",
                {"minimumPlayersToStart", "scoreToWin", "roundDurationTicks", "endedDurationTicks",
                 "resourceArchetypeId", "resourceCircleRadius", "resourceScoreValue", "resourceDensityPerUnit2",
                 "boostInitialLengthCostPerSecond", "activeAreaStartRatio", "activeAreaEndRatio"},
                "gameplay", outError);
            if (channel == nullptr || logging == nullptr || network == nullptr || execution == nullptr ||
                simulation == nullptr || replication == nullptr || gameplay == nullptr)
            {
                return false;
            }

            const JsonObject* const payloadPools =
                ReadObject(*network, "payloadPools",
                           {"payload64BlockCount", "payload256BlockCount", "payload1024BlockCount",
                            "payload8192BlockCount", "payloadRefControlBlockCount"},
                           "network.payloadPools", outError);
            if (payloadPools == nullptr)
            {
                return false;
            }

            const JsonObject* const physics =
                ReadObject(*simulation, "physics", {"maxContactsPerTick", "collisionEpsilon", "contactTolerance"},
                           "simulation.physics", outError);
            const JsonObject* const arena = ReadObject(
                *simulation, "arena", {"minimumX", "minimumY", "maximumX", "maximumY"}, "simulation.arena", outError);
            const JsonObject* const movement =
                ReadObject(*simulation, "movement", {"baseMoveSpeed", "boostMoveSpeed", "angularSpeedRadiansPerSecond"},
                           "simulation.movement", outError);
            const JsonObject* const growth = ReadObject(
                *simulation, "growth", {"initialLength", "lengthPerPoint", "initialDiameter", "diameterPerPoint"},
                "simulation.growth", outError);
            const JsonObject* const body =
                ReadObject(*simulation, "body", {"sampleIntervalTicks", "maxSampleCount"}, "simulation.body", outError);
            const JsonObject* const player = ReadObject(
                *simulation, "player", {"archetypeId", "circleRadius", "spawnX", "spawnY", "spawnMaxCandidatesPerTick"},
                "simulation.player", outError);
            if (physics == nullptr || arena == nullptr || movement == nullptr || growth == nullptr || body == nullptr ||
                player == nullptr)
            {
                return false;
            }

            WorldServerHostConfig config{};
            std::string minimumSeverity;
            std::string bindAddress;
            if (!ReadNumber(*channel, "id", "channel.id", &config.channel.id, outError) ||
                !ReadString(*channel, "name", "channel.name", &config.channel.name, outError))
            {
                return false;
            }
            config.consumer.join.channelId = config.channel.id;
            if (!ReadString(*logging, "minimumSeverity", "logging.minimumSeverity", &minimumSeverity, outError) ||
                !TryParseLogLevel(minimumSeverity, &config.minimumLogSeverity))
            {
                *outError = "logging.minimumSeverity is invalid";
                return false;
            }
            if (!ReadString(*network, "bindAddress", "network.bindAddress", &bindAddress, outError) ||
                !TryParseIpv4(bindAddress, &config.network.bindAddress))
            {
                *outError = "network.bindAddress must be an IPv4 address";
                return false;
            }
            if (!ReadNumber(*network, "port", "network.port", &config.network.port, outError) ||
                !ReadNumber(*network, "listenBacklog", "network.listenBacklog", &config.network.listenBacklog,
                            outError) ||
                !ReadNumber(*network, "acceptSlotCount", "network.acceptSlotCount", &config.network.acceptSlotCount,
                            outError) ||
                !ReadNumber(*network, "actorMailboxCapacity", "network.actorMailboxCapacity",
                            &config.network.actorMailboxCapacity, outError) ||
                !ReadNumber(*network, "pendingSendQueueCapacity", "network.pendingSendQueueCapacity",
                            &config.network.pendingSendQueueCapacity, outError) ||
                !ReadNumber(*network, "maxSessionCount", "network.maxSessionCount", &config.network.maxSessionCount,
                            outError) ||
                !ReadNumber(*network, "toWorldEventCapacity", "network.toWorldEventCapacity",
                            &config.network.toWorldEventCapacity, outError) ||
                !ReadNumber(*payloadPools, "payload64BlockCount", "network.payloadPools.payload64BlockCount",
                            &config.network.payloadPools.payload64BlockCount, outError) ||
                !ReadNumber(*payloadPools, "payload256BlockCount", "network.payloadPools.payload256BlockCount",
                            &config.network.payloadPools.payload256BlockCount, outError) ||
                !ReadNumber(*payloadPools, "payload1024BlockCount", "network.payloadPools.payload1024BlockCount",
                            &config.network.payloadPools.payload1024BlockCount, outError) ||
                !ReadNumber(*payloadPools, "payload8192BlockCount", "network.payloadPools.payload8192BlockCount",
                            &config.network.payloadPools.payload8192BlockCount, outError) ||
                !ReadNumber(*payloadPools, "payloadRefControlBlockCount",
                            "network.payloadPools.payloadRefControlBlockCount",
                            &config.network.payloadPools.payloadRefControlBlockCount, outError))
            {
                return false;
            }

            if (!ReadNumber(*execution, "tickRateHz", "execution.tickRateHz", &config.execution.tickRateHz, outError) ||
                !ReadNumber(*execution, "maxCatchUpSteps", "execution.maxCatchUpSteps",
                            &config.execution.maxCatchUpSteps, outError))
            {
                return false;
            }
            if (!ReadNumber(*execution, "inboundEventCapacityPerSlot", "execution.inboundEventCapacityPerSlot",
                            &config.execution.inboundEventCapacityPerSlot, outError) ||
                !ReadNumber(*execution, "outboundRecordCapacityPerSlot", "execution.outboundRecordCapacityPerSlot",
                            &config.execution.outboundCapacityPerSlot.recordCount, outError) ||
                !ReadNumber(*execution, "outboundRecipientCapacityPerSlot",
                            "execution.outboundRecipientCapacityPerSlot",
                            &config.execution.outboundCapacityPerSlot.recipientCount, outError) ||
                !ReadNumber(*execution, "outboundPayloadByteCapacityPerSlot",
                            "execution.outboundPayloadByteCapacityPerSlot",
                            &config.execution.outboundCapacityPerSlot.payloadByteCount, outError) ||
                !ReadNumber(*execution, "shutdownDrainTimeoutMilliseconds",
                            "execution.shutdownDrainTimeoutMilliseconds",
                            &config.execution.shutdownDrainTimeoutMilliseconds, outError) ||
                !ReadNumber(*execution, "tickSampleCapacity", "execution.tickSampleCapacity",
                            &config.execution.tickSampleCapacity, outError) ||
                !ReadNumber(*execution, "tickSampleWriteQueueCapacity", "execution.tickSampleWriteQueueCapacity",
                            &config.execution.tickSampleWriteQueueCapacity, outError) ||
                !ReadNumber(*execution, "runtimeSampleIntervalMilliseconds",
                            "execution.runtimeSampleIntervalMilliseconds",
                            &config.execution.runtimeSampleIntervalMilliseconds, outError) ||
                !ReadNumber(*execution, "runtimeSampleWriteQueueCapacity", "execution.runtimeSampleWriteQueueCapacity",
                            &config.execution.runtimeSampleWriteQueueCapacity, outError))
            {
                return false;
            }

            if (!ReadNumber(*physics, "maxContactsPerTick", "simulation.physics.maxContactsPerTick",
                            &config.tickProcessor.physics.maxContactsPerTick, outError) ||
                !ReadNumber(*physics, "collisionEpsilon", "simulation.physics.collisionEpsilon",
                            &config.tickProcessor.physics.collisionEpsilon, outError) ||
                !ReadNumber(*physics, "contactTolerance", "simulation.physics.contactTolerance",
                            &config.tickProcessor.physics.contactTolerance, outError) ||
                !ReadNumber(*arena, "minimumX", "simulation.arena.minimumX", &config.tickProcessor.arenaBounds.minimumX,
                            outError) ||
                !ReadNumber(*arena, "minimumY", "simulation.arena.minimumY", &config.tickProcessor.arenaBounds.minimumY,
                            outError) ||
                !ReadNumber(*arena, "maximumX", "simulation.arena.maximumX", &config.tickProcessor.arenaBounds.maximumX,
                            outError) ||
                !ReadNumber(*arena, "maximumY", "simulation.arena.maximumY", &config.tickProcessor.arenaBounds.maximumY,
                            outError) ||
                !ReadNumber(*movement, "baseMoveSpeed", "simulation.movement.baseMoveSpeed",
                            &config.tickProcessor.controlMovement.baseSpeed, outError) ||
                !ReadNumber(*movement, "boostMoveSpeed", "simulation.movement.boostMoveSpeed",
                            &config.tickProcessor.controlMovement.boostSpeed, outError) ||
                !ReadNumber(*movement, "angularSpeedRadiansPerSecond",
                            "simulation.movement.angularSpeedRadiansPerSecond",
                            &config.tickProcessor.controlMovement.angularSpeedRadiansPerSecond, outError))
            {
                return false;
            }

            WorldGrowthConfig growthConfig{};
            std::uint32_t bodySampleIntervalTicks = 0;
            std::uint32_t bodyMaxSampleCount = 0;
            std::uint32_t playerArchetypeId = 0;
            float playerCircleRadius = 0.0f;
            float playerSpawnX = 0.0f;
            float playerSpawnY = 0.0f;
            std::uint32_t playerSpawnMaxCandidatesPerTick = 0;
            if (!ReadNumber(*growth, "initialLength", "simulation.growth.initialLength", &growthConfig.initialLength,
                            outError) ||
                !ReadNumber(*growth, "lengthPerPoint", "simulation.growth.lengthPerPoint", &growthConfig.lengthPerPoint,
                            outError) ||
                !ReadNumber(*growth, "initialDiameter", "simulation.growth.initialDiameter",
                            &growthConfig.initialDiameter, outError) ||
                !ReadNumber(*growth, "diameterPerPoint", "simulation.growth.diameterPerPoint",
                            &growthConfig.diameterPerPoint, outError) ||
                !ReadNumber(*body, "sampleIntervalTicks", "simulation.body.sampleIntervalTicks",
                            &bodySampleIntervalTicks, outError) ||
                !ReadNumber(*body, "maxSampleCount", "simulation.body.maxSampleCount", &bodyMaxSampleCount, outError) ||
                !ReadNumber(*player, "archetypeId", "simulation.player.archetypeId", &playerArchetypeId, outError) ||
                !ReadNumber(*player, "circleRadius", "simulation.player.circleRadius", &playerCircleRadius, outError) ||
                !ReadNumber(*player, "spawnX", "simulation.player.spawnX", &playerSpawnX, outError) ||
                !ReadNumber(*player, "spawnY", "simulation.player.spawnY", &playerSpawnY, outError) ||
                !ReadNumber(*player, "spawnMaxCandidatesPerTick", "simulation.player.spawnMaxCandidatesPerTick",
                            &playerSpawnMaxCandidatesPerTick, outError))
            {
                return false;
            }

            config.tickProcessor.bodyTrailSample =
                WorldBodyTrailSampleConfig{bodySampleIntervalTicks, bodyMaxSampleCount};
            config.tickProcessor.playerBody = WorldPlayerBodyConfig{growthConfig, bodyMaxSampleCount};
            config.tickProcessor.playerArchetypeId = playerArchetypeId;
            config.tickProcessor.playerSpawnMaxCandidatesPerTick = playerSpawnMaxCandidatesPerTick;

            config.consumer.join.tickRateHz = config.execution.tickRateHz;
            config.consumer.join.arenaMinX = config.tickProcessor.arenaBounds.minimumX;
            config.consumer.join.arenaMinY = config.tickProcessor.arenaBounds.minimumY;
            config.consumer.join.arenaMaxX = config.tickProcessor.arenaBounds.maximumX;
            config.consumer.join.arenaMaxY = config.tickProcessor.arenaBounds.maximumY;
            config.consumer.join.playerArchetypeId = playerArchetypeId;
            config.consumer.join.playerCircleRadius = playerCircleRadius;
            config.consumer.join.playerMaxMoveSpeed = config.tickProcessor.controlMovement.baseSpeed;
            config.consumer.join.playerSpawnX = playerSpawnX;
            config.consumer.join.playerSpawnY = playerSpawnY;
            config.consumer.join.playerBody = config.tickProcessor.playerBody;
            if (!ReadNumber(*replication, "snapshotIntervalTicks", "replication.snapshotIntervalTicks",
                            &config.consumer.join.snapshotIntervalTicks, outError) ||
                !ReadNumber(*replication, "commandSlackTicks", "replication.commandSlackTicks",
                            &config.consumer.join.commandSlackTicks, outError) ||
                !ReadNumber(*replication, "spatialCellSize", "replication.spatialCellSize",
                            &config.consumer.spatial.spatialCellSize, outError) ||
                !ReadNumber(*replication, "aoiEnterRadius", "replication.aoiEnterRadius",
                            &config.consumer.spatial.aoiEnterRadius, outError) ||
                !ReadNumber(*replication, "aoiRetainRadius", "replication.aoiRetainRadius",
                            &config.consumer.spatial.aoiRetainRadius, outError) ||
                !ReadNumber(*replication, "replicationIntervalTicks", "replication.replicationIntervalTicks",
                            &config.consumer.replication.snapshotIntervalTicks, outError) ||
                !ReadNumber(*replication, "firstPlayerId", "replication.firstPlayerId", &config.consumer.firstPlayerId,
                            outError))
            {
                return false;
            }

            config.consumer.gameplay.boostCost.growth = growthConfig;
            if (!ReadNumber(*gameplay, "minimumPlayersToStart", "gameplay.minimumPlayersToStart",
                            &config.consumer.gameplay.minimumPlayersToStart, outError) ||
                !ReadNumber(*gameplay, "scoreToWin", "gameplay.scoreToWin", &config.consumer.gameplay.scoreToWin,
                            outError) ||
                !ReadNumber(*gameplay, "roundDurationTicks", "gameplay.roundDurationTicks",
                            &config.consumer.gameplay.roundDurationTicks, outError) ||
                !ReadNumber(*gameplay, "endedDurationTicks", "gameplay.endedDurationTicks",
                            &config.consumer.gameplay.endedDurationTicks, outError) ||
                !ReadNumber(*gameplay, "resourceArchetypeId", "gameplay.resourceArchetypeId",
                            &config.consumer.gameplay.resourceArchetypeId, outError) ||
                !ReadNumber(*gameplay, "resourceCircleRadius", "gameplay.resourceCircleRadius",
                            &config.consumer.gameplay.resourceCircleRadius, outError) ||
                !ReadNumber(*gameplay, "resourceScoreValue", "gameplay.resourceScoreValue",
                            &config.consumer.gameplay.resourceScoreValue, outError) ||
                !ReadNumber(*gameplay, "resourceDensityPerUnit2", "gameplay.resourceDensityPerUnit2",
                            &config.consumer.gameplay.resourceDensityPerUnit2, outError) ||
                !ReadNumber(*gameplay, "boostInitialLengthCostPerSecond", "gameplay.boostInitialLengthCostPerSecond",
                            &config.consumer.gameplay.boostCost.initialLengthCostPerSecond, outError) ||
                !ReadNumber(*gameplay, "activeAreaStartRatio", "gameplay.activeAreaStartRatio",
                            &config.consumer.gameplay.activeAreaStartRatio, outError) ||
                !ReadNumber(*gameplay, "activeAreaEndRatio", "gameplay.activeAreaEndRatio",
                            &config.consumer.gameplay.activeAreaEndRatio, outError))
            {
                return false;
            }

            *outConfig = config;
            return true;
        }

        [[nodiscard]] JsonObject CreateDocument(const WorldServerHostConfig& config)
        {
            const WorldGrowthConfig& growth = config.tickProcessor.playerBody.growth;
            JsonObject document = JsonObject::object();
            document["schema"] = ConfigSchema;
            document["version"] = ConfigVersion;
            document["channel"] = {{"id", config.channel.id}, {"name", config.channel.name}};
            document["logging"] = {{"minimumSeverity", LogLevelName(config.minimumLogSeverity)}};
            document["network"] = {
                {"bindAddress", Ipv4Text(config.network.bindAddress)},
                {"port", config.network.port},
                {"listenBacklog", config.network.listenBacklog},
                {"acceptSlotCount", config.network.acceptSlotCount},
                {"actorMailboxCapacity", config.network.actorMailboxCapacity},
                {"pendingSendQueueCapacity", config.network.pendingSendQueueCapacity},
                {"maxSessionCount", config.network.maxSessionCount},
                {"toWorldEventCapacity", config.network.toWorldEventCapacity},
                {"payloadPools",
                 {{"payload64BlockCount", config.network.payloadPools.payload64BlockCount},
                  {"payload256BlockCount", config.network.payloadPools.payload256BlockCount},
                  {"payload1024BlockCount", config.network.payloadPools.payload1024BlockCount},
                  {"payload8192BlockCount", config.network.payloadPools.payload8192BlockCount},
                  {"payloadRefControlBlockCount", config.network.payloadPools.payloadRefControlBlockCount}}},
            };
            document["execution"] = {
                {"tickRateHz", config.execution.tickRateHz},
                {"maxCatchUpSteps", config.execution.maxCatchUpSteps},
                {"inboundEventCapacityPerSlot", config.execution.inboundEventCapacityPerSlot},
                {"outboundRecordCapacityPerSlot", config.execution.outboundCapacityPerSlot.recordCount},
                {"outboundRecipientCapacityPerSlot", config.execution.outboundCapacityPerSlot.recipientCount},
                {"outboundPayloadByteCapacityPerSlot", config.execution.outboundCapacityPerSlot.payloadByteCount},
                {"shutdownDrainTimeoutMilliseconds", config.execution.shutdownDrainTimeoutMilliseconds},
                {"tickSampleCapacity", config.execution.tickSampleCapacity},
                {"tickSampleWriteQueueCapacity", config.execution.tickSampleWriteQueueCapacity},
                {"runtimeSampleIntervalMilliseconds", config.execution.runtimeSampleIntervalMilliseconds},
                {"runtimeSampleWriteQueueCapacity", config.execution.runtimeSampleWriteQueueCapacity},
            };
            document["simulation"] = {
                {"physics",
                 {{"maxContactsPerTick", config.tickProcessor.physics.maxContactsPerTick},
                  {"collisionEpsilon", config.tickProcessor.physics.collisionEpsilon},
                  {"contactTolerance", config.tickProcessor.physics.contactTolerance}}},
                {"arena",
                 {{"minimumX", config.tickProcessor.arenaBounds.minimumX},
                  {"minimumY", config.tickProcessor.arenaBounds.minimumY},
                  {"maximumX", config.tickProcessor.arenaBounds.maximumX},
                  {"maximumY", config.tickProcessor.arenaBounds.maximumY}}},
                {"movement",
                 {{"baseMoveSpeed", config.tickProcessor.controlMovement.baseSpeed},
                  {"boostMoveSpeed", config.tickProcessor.controlMovement.boostSpeed},
                  {"angularSpeedRadiansPerSecond", config.tickProcessor.controlMovement.angularSpeedRadiansPerSecond}}},
                {"growth",
                 {{"initialLength", growth.initialLength},
                  {"lengthPerPoint", growth.lengthPerPoint},
                  {"initialDiameter", growth.initialDiameter},
                  {"diameterPerPoint", growth.diameterPerPoint}}},
                {"body",
                 {{"sampleIntervalTicks", config.tickProcessor.bodyTrailSample.sampleIntervalTicks},
                  {"maxSampleCount", config.tickProcessor.bodyTrailSample.maxSampleCount}}},
                {"player",
                 {{"archetypeId", config.tickProcessor.playerArchetypeId},
                  {"circleRadius", config.consumer.join.playerCircleRadius},
                  {"spawnX", config.consumer.join.playerSpawnX},
                  {"spawnY", config.consumer.join.playerSpawnY},
                  {"spawnMaxCandidatesPerTick", config.tickProcessor.playerSpawnMaxCandidatesPerTick}}},
            };
            document["replication"] = {
                {"snapshotIntervalTicks", config.consumer.join.snapshotIntervalTicks},
                {"commandSlackTicks", config.consumer.join.commandSlackTicks},
                {"spatialCellSize", config.consumer.spatial.spatialCellSize},
                {"aoiEnterRadius", config.consumer.spatial.aoiEnterRadius},
                {"aoiRetainRadius", config.consumer.spatial.aoiRetainRadius},
                {"replicationIntervalTicks", config.consumer.replication.snapshotIntervalTicks},
                {"firstPlayerId", config.consumer.firstPlayerId},
            };
            document["gameplay"] = {
                {"minimumPlayersToStart", config.consumer.gameplay.minimumPlayersToStart},
                {"scoreToWin", config.consumer.gameplay.scoreToWin},
                {"roundDurationTicks", config.consumer.gameplay.roundDurationTicks},
                {"endedDurationTicks", config.consumer.gameplay.endedDurationTicks},
                {"resourceArchetypeId", config.consumer.gameplay.resourceArchetypeId},
                {"resourceCircleRadius", config.consumer.gameplay.resourceCircleRadius},
                {"resourceScoreValue", config.consumer.gameplay.resourceScoreValue},
                {"resourceDensityPerUnit2", config.consumer.gameplay.resourceDensityPerUnit2},
                {"boostInitialLengthCostPerSecond", config.consumer.gameplay.boostCost.initialLengthCostPerSecond},
                {"activeAreaStartRatio", config.consumer.gameplay.activeAreaStartRatio},
                {"activeAreaEndRatio", config.consumer.gameplay.activeAreaEndRatio},
            };
            return document;
        }
    } // namespace

    WorldResult<WorldServerHostConfig, std::string> WorldServerHostConfigSource::Load(const std::filesystem::path& path)
    {
        if (path.empty())
        {
            return WorldResult<WorldServerHostConfig, std::string>::Failure("config path must not be empty");
        }

        std::ifstream input(path, std::ios::binary);
        if (!input.is_open())
        {
            return WorldResult<WorldServerHostConfig, std::string>::Failure("failed to open World Host config: " +
                                                                            path.string());
        }

        std::ostringstream buffer;
        buffer << input.rdbuf();
        if (!input.good() && !input.eof())
        {
            return WorldResult<WorldServerHostConfig, std::string>::Failure("failed to read World Host config: " +
                                                                            path.string());
        }

        try
        {
            const JsonObject document = JsonObject::parse(buffer.str());
            WorldServerHostConfig config{};
            std::string error;
            if (!ParseDocument(document, &config, &error))
            {
                return WorldResult<WorldServerHostConfig, std::string>::Failure(std::move(error));
            }
            error = Validate(config);
            if (!error.empty())
            {
                return WorldResult<WorldServerHostConfig, std::string>::Failure(std::move(error));
            }

            return WorldResult<WorldServerHostConfig, std::string>{std::move(config)};
        }
        catch (const nlohmann::json::exception& error)
        {
            return WorldResult<WorldServerHostConfig, std::string>::Failure(
                std::string("failed to parse World Host config: ") + error.what());
        }
        catch (const std::exception& error)
        {
            return WorldResult<WorldServerHostConfig, std::string>::Failure(
                std::string("failed to load World Host config: ") + error.what());
        }
    }

    std::string WorldServerHostConfigSource::Validate(const WorldServerHostConfig& config)
    {
        if (config.channel.id == 0 || config.channel.name.empty() ||
            config.channel.name.size() > WorldServerHostChannelConfig::MaximumNameBytes ||
            config.channel.name.find_first_not_of(" \t\r\n") == std::string::npos)
        {
            return "channel id and name must be valid";
        }
        const std::string_view minimumSeverity = LogLevelName(config.minimumLogSeverity);
        if (minimumSeverity.empty())
        {
            return "logging.minimumSeverity is invalid";
        }
        if (config.network.port == 0 || config.network.listenBacklog <= 0 || config.network.acceptSlotCount == 0 ||
            config.network.actorMailboxCapacity == 0 || config.network.pendingSendQueueCapacity == 0 ||
            config.network.maxSessionCount == 0 || config.network.toWorldEventCapacity == 0 ||
            config.network.payloadPools.payload64BlockCount == 0 ||
            config.network.payloadPools.payload256BlockCount == 0 ||
            config.network.payloadPools.payload1024BlockCount == 0 ||
            config.network.payloadPools.payload8192BlockCount == 0 ||
            config.network.payloadPools.payloadRefControlBlockCount == 0)
        {
            return "network capacities must be greater than zero";
        }
        if (config.execution.tickRateHz == 0 || config.execution.maxCatchUpSteps == 0 ||
            config.execution.inboundEventCapacityPerSlot == 0 ||
            config.execution.outboundCapacityPerSlot.recordCount == 0 ||
            config.execution.outboundCapacityPerSlot.recipientCount == 0 ||
            config.execution.outboundCapacityPerSlot.payloadByteCount == 0 ||
            config.execution.shutdownDrainTimeoutMilliseconds == 0 || config.execution.tickSampleCapacity == 0 ||
            config.execution.tickSampleWriteQueueCapacity == 0 ||
            config.execution.runtimeSampleIntervalMilliseconds == 0 ||
            config.execution.runtimeSampleWriteQueueCapacity == 0)
        {
            return "execution schedule, capacities, and timeout must be valid";
        }
        if (!IsValid(config.tickProcessor.physics) ||
            !WorldPhysicsArenaBounds::IsValid(config.tickProcessor.arenaBounds) ||
            !WorldControlMovementSolver::IsValidConfig(config.tickProcessor.controlMovement) ||
            !WorldPlayerBody::IsValidConfig(config.tickProcessor.playerBody) ||
            config.tickProcessor.bodyTrailSample.sampleIntervalTicks == 0 ||
            config.tickProcessor.bodyTrailSample.maxSampleCount == 0 ||
            config.tickProcessor.bodyTrailSample.maxSampleCount !=
                config.tickProcessor.playerBody.maxTrailSampleCount ||
            config.tickProcessor.playerArchetypeId == 0 || config.tickProcessor.playerSpawnMaxCandidatesPerTick == 0)
        {
            return "simulation physics, movement, body, or player config is invalid";
        }
        if (config.consumer.join.channelId != config.channel.id ||
            config.consumer.join.tickRateHz != config.execution.tickRateHz ||
            config.consumer.join.snapshotIntervalTicks == 0 || config.consumer.join.commandSlackTicks == 0 ||
            config.consumer.replication.snapshotIntervalTicks != config.consumer.join.snapshotIntervalTicks ||
            config.consumer.join.arenaMinX != config.tickProcessor.arenaBounds.minimumX ||
            config.consumer.join.arenaMinY != config.tickProcessor.arenaBounds.minimumY ||
            config.consumer.join.arenaMaxX != config.tickProcessor.arenaBounds.maximumX ||
            config.consumer.join.arenaMaxY != config.tickProcessor.arenaBounds.maximumY ||
            config.consumer.join.playerArchetypeId != config.tickProcessor.playerArchetypeId ||
            config.consumer.join.playerBody != config.tickProcessor.playerBody ||
            !std::isfinite(config.consumer.join.playerCircleRadius) ||
            config.consumer.join.playerCircleRadius <= 0.0f ||
            std::fabs(config.consumer.join.playerCircleRadius * 2.0f -
                      config.tickProcessor.playerBody.growth.initialDiameter) > 0.0001f ||
            !std::isfinite(config.consumer.join.playerSpawnX) || !std::isfinite(config.consumer.join.playerSpawnY) ||
            config.consumer.firstPlayerId == 0)
        {
            return "join config is inconsistent with execution or player config";
        }
        if (!std::isfinite(config.consumer.spatial.spatialCellSize) ||
            config.consumer.spatial.spatialCellSize <= 0.0f || !std::isfinite(config.consumer.spatial.aoiEnterRadius) ||
            config.consumer.spatial.aoiEnterRadius <= 0.0f || !std::isfinite(config.consumer.spatial.aoiRetainRadius) ||
            config.consumer.spatial.aoiRetainRadius < config.consumer.spatial.aoiEnterRadius ||
            config.consumer.replication.snapshotIntervalTicks == 0)
        {
            return "replication spatial and cadence config is invalid";
        }
        if (config.consumer.gameplay.boostCost.growth != config.tickProcessor.playerBody.growth ||
            !IsValid(config.consumer.gameplay, config.tickProcessor.arenaBounds))
        {
            return "gameplay config is invalid or inconsistent with player growth";
        }
        return {};
    }

    WorldResult<std::string, std::string> WorldServerHostConfigSource::SerializeNormalized(
        const WorldServerHostConfig& config)
    {
        try
        {
            return WorldResult<std::string, std::string>{CreateDocument(config).dump()};
        }
        catch (const std::exception& error)
        {
            return WorldResult<std::string, std::string>::Failure(
                std::string("failed to serialize effective World Host config: ") + error.what());
        }
    }

    WorldResult<void, std::string> WorldServerHostConfigSource::WriteNormalized(const std::filesystem::path& path,
                                                                                const std::string_view normalizedJson)
    {
        if (path.empty() || normalizedJson.empty())
        {
            return WorldResult<void, std::string>::Failure("effective config path and payload must not be empty");
        }

        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output.is_open())
        {
            return WorldResult<void, std::string>::Failure("failed to open effective World Host config output: " +
                                                           path.string());
        }
        output.write(normalizedJson.data(), static_cast<std::streamsize>(normalizedJson.size()));
        output.put('\n');
        output.flush();
        if (!output.good())
        {
            return WorldResult<void, std::string>::Failure("failed to write effective World Host config: " +
                                                           path.string());
        }
        return WorldResult<void, std::string>::Success();
    }
} // namespace psnr::world::host
