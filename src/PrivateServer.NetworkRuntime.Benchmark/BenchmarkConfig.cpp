#include "BenchmarkConfig.h"

#include "BenchmarkProtocol.h"

#include <PrivateServer/NetworkRuntime/NrPacketHeader.h>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace psnr::benchmark
{
    namespace
    {
        constexpr std::string_view ConfigSchemaName = "psnr.network_runtime.benchmark.config";
        constexpr std::uint64_t ConfigSchemaVersion = 1;

        constexpr std::string_view JsonKeySchema = "schema";
        constexpr std::string_view JsonKeyVersion = "version";
        constexpr std::string_view JsonKeyProfile = "profile";
        constexpr std::string_view JsonKeyId = "id";
        constexpr std::string_view JsonKeyPurpose = "purpose";
        constexpr std::string_view JsonKeyServer = "server";
        constexpr std::string_view JsonKeyAddress = "address";
        constexpr std::string_view JsonKeyPort = "port";
        constexpr std::string_view JsonKeyConnection = "connection";
        constexpr std::string_view JsonKeyClientCount = "clientCount";
        constexpr std::string_view JsonKeyBatchSize = "batchSize";
        constexpr std::string_view JsonKeyTimeoutSeconds = "timeoutSeconds";
        constexpr std::string_view JsonKeyWorkload = "workload";
        constexpr std::string_view JsonKeyOperation = "operation";
        constexpr std::string_view JsonKeyRequestRatePerClient = "requestRatePerClient";
        constexpr std::string_view JsonKeyWorkerCount = "workerCount";
        constexpr std::string_view JsonKeySemanticPayloadBytes = "semanticPayloadBytes";
        constexpr std::string_view JsonKeyPhases = "phases";
        constexpr std::string_view JsonKeyWarmupSeconds = "warmupSeconds";
        constexpr std::string_view JsonKeyMeasurementSeconds = "measurementSeconds";
        constexpr std::string_view JsonKeyDrainTimeoutSeconds = "drainTimeoutSeconds";
        constexpr std::string_view JsonKeyShutdownTimeoutSeconds = "shutdownTimeoutSeconds";
        constexpr std::string_view JsonKeySampling = "sampling";
        constexpr std::string_view JsonKeyIntervalMs = "intervalMs";
        constexpr std::string_view JsonKeyExecution = "execution";
        constexpr std::string_view JsonKeyRepeats = "repeats";
        constexpr std::string_view JsonKeyArtifact = "artifact";
        constexpr std::string_view JsonKeyFormat = "format";
        constexpr std::string_view JsonKeyOutputRoot = "outputRoot";

        using JsonObject = nlohmann::ordered_json;

        [[nodiscard]] const JsonObject* FindProperty(const JsonObject& object, const std::string_view key)
        {
            const JsonObject::const_iterator iterator = object.find(key);
            return iterator == object.end() ? nullptr : &(*iterator);
        }

        [[nodiscard]] const JsonObject* ReadObject(const JsonObject& parent, const std::string_view key,
                                                   const std::string_view path, std::string* outError)
        {
            if (outError == nullptr)
            {
                return nullptr;
            }

            const JsonObject* value = FindProperty(parent, key);
            if (value == nullptr)
            {
                *outError = std::string(path) + " is required";
                return nullptr;
            }
            if (!value->is_object())
            {
                *outError = std::string(path) + " must be an object";
                return nullptr;
            }
            return value;
        }

        [[nodiscard]] bool ReadString(const JsonObject& parent, const std::string_view key, const std::string_view path,
                                      std::string* outValue, std::string* outError)
        {
            if (outValue == nullptr || outError == nullptr)
            {
                return false;
            }

            const JsonObject* value = FindProperty(parent, key);
            if (value == nullptr)
            {
                *outError = std::string(path) + " is required";
                return false;
            }
            if (!value->is_string())
            {
                *outError = std::string(path) + " must be a string";
                return false;
            }

            *outValue = value->get<std::string>();
            return true;
        }

        template <typename TValue>
        [[nodiscard]] bool ReadUnsigned(const JsonObject& parent, const std::string_view key,
                                        const std::string_view path, TValue* outValue, std::string* outError)
        {
            static_assert(std::is_unsigned_v<TValue>);
            if (outValue == nullptr || outError == nullptr)
            {
                return false;
            }

            const JsonObject* value = FindProperty(parent, key);
            if (value == nullptr)
            {
                *outError = std::string(path) + " is required";
                return false;
            }
            if (!value->is_number_unsigned())
            {
                *outError = std::string(path) + " must be an unsigned integer";
                return false;
            }

            const std::uint64_t raw = value->get<std::uint64_t>();
            if (raw > static_cast<std::uint64_t>(std::numeric_limits<TValue>::max()))
            {
                *outError = std::string(path) + " is out of range";
                return false;
            }

            *outValue = static_cast<TValue>(raw);
            return true;
        }

        [[nodiscard]] BenchmarkConfigParseResult Failure(std::string error)
        {
            BenchmarkConfigParseResult result;
            result.error = std::move(error);
            return result;
        }
    } // namespace

    BenchmarkConfigV1 BenchmarkConfigV1Codec::Canonical()
    {
        return BenchmarkConfigV1{};
    }

    BenchmarkConfigParseResult BenchmarkConfigV1Codec::Parse(const std::string_view jsonText)
    {
        if (jsonText.empty())
        {
            return Failure("config JSON is empty");
        }

        JsonObject document = JsonObject::parse(jsonText, nullptr, false);
        if (document.is_discarded())
        {
            return Failure("config JSON is malformed");
        }
        if (!document.is_object())
        {
            return Failure("config root must be an object");
        }

        BenchmarkConfigV1 config;
        std::string error;
        std::string schema;
        std::uint64_t version = 0;
        if (!ReadString(document, JsonKeySchema, "schema", &schema, &error) ||
            !ReadUnsigned(document, JsonKeyVersion, "version", &version, &error))
        {
            return Failure(std::move(error));
        }
        if (schema != ConfigSchemaName)
        {
            return Failure("schema is unsupported");
        }
        if (version != ConfigSchemaVersion)
        {
            return Failure("version is unsupported");
        }

        const JsonObject* profile = ReadObject(document, JsonKeyProfile, "profile", &error);
        const JsonObject* server = ReadObject(document, JsonKeyServer, "server", &error);
        const JsonObject* connection = ReadObject(document, JsonKeyConnection, "connection", &error);
        const JsonObject* workload = ReadObject(document, JsonKeyWorkload, "workload", &error);
        const JsonObject* phases = ReadObject(document, JsonKeyPhases, "phases", &error);
        const JsonObject* sampling = ReadObject(document, JsonKeySampling, "sampling", &error);
        const JsonObject* execution = ReadObject(document, JsonKeyExecution, "execution", &error);
        const JsonObject* artifact = ReadObject(document, JsonKeyArtifact, "artifact", &error);
        if (profile == nullptr || server == nullptr || connection == nullptr || workload == nullptr ||
            phases == nullptr || sampling == nullptr || execution == nullptr || artifact == nullptr)
        {
            return Failure(std::move(error));
        }

        if (!ReadString(*profile, JsonKeyId, "profile.id", &config.profile.id, &error) ||
            !ReadUnsigned(*profile, JsonKeyVersion, "profile.version", &config.profile.version, &error) ||
            !ReadString(*profile, JsonKeyPurpose, "profile.purpose", &config.profile.purpose, &error) ||
            !ReadString(*server, JsonKeyAddress, "server.address", &config.server.address, &error) ||
            !ReadUnsigned(*server, JsonKeyPort, "server.port", &config.server.port, &error) ||
            !ReadUnsigned(*connection, JsonKeyClientCount, "connection.clientCount", &config.connection.clientCount,
                          &error) ||
            !ReadUnsigned(*connection, JsonKeyBatchSize, "connection.batchSize", &config.connection.batchSize,
                          &error) ||
            !ReadUnsigned(*connection, JsonKeyTimeoutSeconds, "connection.timeoutSeconds",
                          &config.connection.timeoutSeconds, &error) ||
            !ReadString(*workload, JsonKeyOperation, "workload.operation", &config.workload.operation, &error) ||
            !ReadUnsigned(*workload, JsonKeyRequestRatePerClient, "workload.requestRatePerClient",
                          &config.workload.requestRatePerClient, &error) ||
            !ReadUnsigned(*workload, JsonKeyWorkerCount, "workload.workerCount", &config.workload.workerCount,
                          &error) ||
            !ReadUnsigned(*workload, JsonKeySemanticPayloadBytes, "workload.semanticPayloadBytes",
                          &config.workload.semanticPayloadBytes, &error) ||
            !ReadUnsigned(*phases, JsonKeyWarmupSeconds, "phases.warmupSeconds", &config.phases.warmupSeconds,
                          &error) ||
            !ReadUnsigned(*phases, JsonKeyMeasurementSeconds, "phases.measurementSeconds",
                          &config.phases.measurementSeconds, &error) ||
            !ReadUnsigned(*phases, JsonKeyDrainTimeoutSeconds, "phases.drainTimeoutSeconds",
                          &config.phases.drainTimeoutSeconds, &error) ||
            !ReadUnsigned(*phases, JsonKeyShutdownTimeoutSeconds, "phases.shutdownTimeoutSeconds",
                          &config.phases.shutdownTimeoutSeconds, &error) ||
            !ReadUnsigned(*sampling, JsonKeyIntervalMs, "sampling.intervalMs", &config.sampling.intervalMs, &error) ||
            !ReadUnsigned(*execution, JsonKeyRepeats, "execution.repeats", &config.execution.repeats, &error) ||
            !ReadString(*artifact, JsonKeyFormat, "artifact.format", &config.artifact.format, &error) ||
            !ReadString(*artifact, JsonKeyOutputRoot, "artifact.outputRoot", &config.artifact.outputRoot, &error))
        {
            return Failure(std::move(error));
        }

        error = Validate(config);
        if (!error.empty())
        {
            return Failure(std::move(error));
        }

        BenchmarkConfigParseResult result;
        result.config = std::move(config);
        return result;
    }

    std::string BenchmarkConfigV1Codec::Validate(const BenchmarkConfigV1& config)
    {
        if (config.profile.id != "steady-roundtrip" || config.profile.version != 1)
        {
            return "profile identity is unsupported";
        }
        if (config.profile.purpose.empty())
        {
            return "profile.purpose must not be empty";
        }
        if (config.server.address.empty() || config.server.port == 0)
        {
            return "server address and port must be configured";
        }
        if (config.connection.clientCount == 0 || config.connection.batchSize == 0 ||
            config.connection.batchSize > config.connection.clientCount || config.connection.timeoutSeconds == 0)
        {
            return "connection values are invalid";
        }
        if (config.workload.operation != "echo" || config.workload.requestRatePerClient == 0 ||
            config.workload.workerCount == 0 || config.workload.workerCount > config.connection.clientCount)
        {
            return "workload operation, request rate, or worker count is invalid";
        }

        const std::size_t maximumPayloadBytes = psnr::core::NrMaxPacketLength - psnr::core::NrPacketHeaderLength;
        if (config.workload.semanticPayloadBytes < BenchmarkPayloadFieldBytes ||
            config.workload.semanticPayloadBytes > maximumPayloadBytes)
        {
            return "workload.semanticPayloadBytes is outside the public packet range";
        }
        if (config.phases.measurementSeconds == 0 || config.phases.drainTimeoutSeconds == 0 ||
            config.phases.shutdownTimeoutSeconds == 0)
        {
            return "measurement and timeout phases must be greater than zero";
        }
        if (config.sampling.intervalMs == 0 || config.execution.repeats == 0)
        {
            return "sampling interval and repeats must be greater than zero";
        }
        if (config.artifact.format != "jsonl" || config.artifact.outputRoot.empty())
        {
            return "artifact format or output root is invalid";
        }

        return {};
    }

    std::string BenchmarkConfigV1Codec::SerializeNormalized(const BenchmarkConfigV1& config)
    {
        JsonObject document = JsonObject::object();
        document[JsonKeySchema] = std::string(ConfigSchemaName);
        document[JsonKeyVersion] = ConfigSchemaVersion;

        JsonObject& profile = document[JsonKeyProfile];
        profile[JsonKeyId] = config.profile.id;
        profile[JsonKeyVersion] = config.profile.version;
        profile[JsonKeyPurpose] = config.profile.purpose;

        JsonObject& server = document[JsonKeyServer];
        server[JsonKeyAddress] = config.server.address;
        server[JsonKeyPort] = config.server.port;

        JsonObject& connection = document[JsonKeyConnection];
        connection[JsonKeyClientCount] = config.connection.clientCount;
        connection[JsonKeyBatchSize] = config.connection.batchSize;
        connection[JsonKeyTimeoutSeconds] = config.connection.timeoutSeconds;

        JsonObject& workload = document[JsonKeyWorkload];
        workload[JsonKeyOperation] = config.workload.operation;
        workload[JsonKeyRequestRatePerClient] = config.workload.requestRatePerClient;
        workload[JsonKeyWorkerCount] = config.workload.workerCount;
        workload[JsonKeySemanticPayloadBytes] = config.workload.semanticPayloadBytes;

        JsonObject& phases = document[JsonKeyPhases];
        phases[JsonKeyWarmupSeconds] = config.phases.warmupSeconds;
        phases[JsonKeyMeasurementSeconds] = config.phases.measurementSeconds;
        phases[JsonKeyDrainTimeoutSeconds] = config.phases.drainTimeoutSeconds;
        phases[JsonKeyShutdownTimeoutSeconds] = config.phases.shutdownTimeoutSeconds;

        JsonObject& sampling = document[JsonKeySampling];
        sampling[JsonKeyIntervalMs] = config.sampling.intervalMs;

        JsonObject& execution = document[JsonKeyExecution];
        execution[JsonKeyRepeats] = config.execution.repeats;

        JsonObject& artifact = document[JsonKeyArtifact];
        artifact[JsonKeyFormat] = config.artifact.format;
        artifact[JsonKeyOutputRoot] = config.artifact.outputRoot;

        return document.dump(2);
    }
} // namespace psnr::benchmark
