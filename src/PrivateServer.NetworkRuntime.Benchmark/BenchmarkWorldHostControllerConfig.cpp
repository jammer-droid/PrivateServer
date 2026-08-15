#include "BenchmarkWorldHostControllerConfig.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace psnr::benchmark
{
    namespace
    {
        using JsonObject = nlohmann::ordered_json;
        using ConfigContract = BenchmarkWorldHostControllerConfigContract;

        [[nodiscard]] BenchmarkWorldHostControllerConfigResolveResult Failure(std::string error)
        {
            BenchmarkWorldHostControllerConfigResolveResult result;
            result.error = std::move(error);
            return result;
        }

        [[nodiscard]] const JsonObject* FindProperty(const JsonObject& object, const std::string_view key)
        {
            const JsonObject::const_iterator iterator = object.find(key);
            return iterator == object.end() ? nullptr : &(*iterator);
        }

        [[nodiscard]] bool ReadString(const JsonObject& object, const std::string_view key, const std::string_view path,
                                      std::string* const outValue, std::string* const outError)
        {
            const JsonObject* const value = FindProperty(object, key);
            if (value == nullptr || !value->is_string() || value->get_ref<const std::string&>().empty())
            {
                *outError = std::string{path} + " must be a non-empty string";
                return false;
            }
            *outValue = value->get<std::string>();
            return true;
        }

        [[nodiscard]] bool ReadUnsigned(const JsonObject& object, const std::string_view key,
                                        const std::string_view path, std::uint32_t* const outValue,
                                        std::string* const outError)
        {
            const JsonObject* const value = FindProperty(object, key);
            if (value == nullptr || !value->is_number_unsigned())
            {
                *outError = std::string{path} + " must be an unsigned integer";
                return false;
            }
            const std::uint64_t rawValue = value->get<std::uint64_t>();
            if (rawValue == 0 || rawValue > std::numeric_limits<std::uint32_t>::max())
            {
                *outError = std::string{path} + " is out of range";
                return false;
            }
            *outValue = static_cast<std::uint32_t>(rawValue);
            return true;
        }

        [[nodiscard]] std::filesystem::path Utf8Path(const std::string_view value)
        {
            const std::u8string encoded(value.cbegin(), value.cend());
            return std::filesystem::path{encoded};
        }

        [[nodiscard]] std::string Utf8String(const std::filesystem::path& value)
        {
            const std::u8string encoded = value.generic_u8string();
            return std::string{encoded.cbegin(), encoded.cend()};
        }

        [[nodiscard]] std::filesystem::path ResolvePath(const std::filesystem::path& configDirectory,
                                                        const std::string_view value)
        {
            const std::filesystem::path path = Utf8Path(value);
            return (path.is_absolute() ? path : configDirectory / path).lexically_normal();
        }

        [[nodiscard]] bool IsRegularFile(const std::filesystem::path& path, std::string* const outError)
        {
            std::error_code filesystemError;
            const bool isRegularFile = std::filesystem::is_regular_file(path, filesystemError);
            if (filesystemError || !isRegularFile)
            {
                *outError = "configured file does not exist: " + Utf8String(path);
                return false;
            }
            return true;
        }

        [[nodiscard]] constexpr bool IsPowerOfTwo(const std::uint32_t value) noexcept
        {
            return value >= 2 && (value & (value - 1)) == 0;
        }
    } // namespace

    BenchmarkWorldHostControllerConfigResolveResult BenchmarkWorldHostControllerConfig::Resolve(
        const std::string_view configPath)
    {
        try
        {
            if (configPath.empty())
            {
                return Failure("World Host controller config path is required");
            }

            const std::filesystem::path absoluteConfigPath = std::filesystem::absolute(Utf8Path(configPath));
            std::error_code fileSizeError;
            const std::uintmax_t fileSize = std::filesystem::file_size(absoluteConfigPath, fileSizeError);
            if (fileSizeError)
            {
                return Failure("World Host controller config file is unavailable");
            }
            if (fileSize > ConfigContract::MaximumDocumentBytes || fileSize > std::numeric_limits<std::size_t>::max())
            {
                return Failure("World Host controller config exceeds the 1 MiB limit");
            }

            std::ifstream input{absoluteConfigPath, std::ios::binary};
            if (!input.is_open())
            {
                return Failure("World Host controller config file open failed");
            }
            std::string jsonText(static_cast<std::size_t>(fileSize), '\0');
            if (!jsonText.empty())
            {
                input.read(jsonText.data(), static_cast<std::streamsize>(jsonText.size()));
                if (input.gcount() != static_cast<std::streamsize>(jsonText.size()) || input.bad())
                {
                    return Failure("World Host controller config file read failed");
                }
            }

            JsonObject document = JsonObject::parse(jsonText, nullptr, false);
            if (document.is_discarded() || !document.is_object())
            {
                return Failure("World Host controller config JSON is malformed");
            }
            const JsonObject* const schema = FindProperty(document, ConfigContract::Schema.key);
            const JsonObject* const version = FindProperty(document, ConfigContract::Version.key);
            const JsonObject* const worldHost = FindProperty(document, ConfigContract::WorldHostSection);
            const JsonObject* const artifact = FindProperty(document, ConfigContract::ArtifactSection);
            const JsonObject* const clients = FindProperty(document, ConfigContract::ClientsSection);
            const JsonObject* const workload = FindProperty(document, ConfigContract::WorkloadSection);
            const JsonObject* const sampling = FindProperty(document, ConfigContract::SamplingSection);
            const JsonObject* const lifecycle = FindProperty(document, ConfigContract::LifecycleSection);
            if (schema == nullptr || !schema->is_string() || schema->get<std::string>() != ConfigContract::SchemaName ||
                version == nullptr || !version->is_number_unsigned() ||
                version->get<std::uint64_t>() != ConfigContract::SchemaVersion || worldHost == nullptr ||
                !worldHost->is_object() || artifact == nullptr || !artifact->is_object() || clients == nullptr ||
                !clients->is_object() || workload == nullptr || !workload->is_object() || sampling == nullptr ||
                !sampling->is_object() || lifecycle == nullptr || !lifecycle->is_object())
            {
                return Failure("World Host controller config envelope is invalid");
            }

            std::string executablePath;
            std::string serverConfigPath;
            std::string runsRoot;
            std::string clientAddress;
            std::uint32_t clientPort = 0;
            std::uint32_t clientCount = 0;
            std::uint32_t clientRampPerSecond = 0;
            std::uint32_t clientAdmissionTimeoutMilliseconds = 0;
            std::uint32_t clientRoundResultTimeoutMilliseconds = 0;
            std::uint32_t clientEventQueueCapacity = 0;
            std::uint32_t clientPayloadQueueCapacity = 0;
            std::uint32_t workloadSeed = 0;
            std::uint32_t controlIntervalMilliseconds = 0;
            std::uint32_t boostPercent = 0;
            std::uint32_t samplingIntervalMilliseconds = 0;
            std::uint32_t shutdownTimeoutMilliseconds = 0;
            std::string error;
            if (!ReadString(*worldHost, ConfigContract::ExecutablePath.key, ConfigContract::ExecutablePath.path,
                            &executablePath, &error) ||
                !ReadString(*worldHost, ConfigContract::ServerConfigPath.key, ConfigContract::ServerConfigPath.path,
                            &serverConfigPath, &error) ||
                !ReadString(*artifact, ConfigContract::RunsRoot.key, ConfigContract::RunsRoot.path, &runsRoot,
                            &error) ||
                !ReadString(*clients, ConfigContract::ClientAddress.key, ConfigContract::ClientAddress.path,
                            &clientAddress, &error) ||
                !ReadUnsigned(*clients, ConfigContract::ClientPort.key, ConfigContract::ClientPort.path, &clientPort,
                              &error) ||
                !ReadUnsigned(*clients, ConfigContract::ClientCount.key, ConfigContract::ClientCount.path, &clientCount,
                              &error) ||
                !ReadUnsigned(*clients, ConfigContract::ClientRampPerSecond.key,
                              ConfigContract::ClientRampPerSecond.path, &clientRampPerSecond, &error) ||
                !ReadUnsigned(*clients, ConfigContract::ClientAdmissionTimeoutMilliseconds.key,
                              ConfigContract::ClientAdmissionTimeoutMilliseconds.path,
                              &clientAdmissionTimeoutMilliseconds, &error) ||
                !ReadUnsigned(*clients, ConfigContract::ClientRoundResultTimeoutMilliseconds.key,
                              ConfigContract::ClientRoundResultTimeoutMilliseconds.path,
                              &clientRoundResultTimeoutMilliseconds, &error) ||
                !ReadUnsigned(*clients, ConfigContract::ClientEventQueueCapacity.key,
                              ConfigContract::ClientEventQueueCapacity.path, &clientEventQueueCapacity, &error) ||
                !ReadUnsigned(*clients, ConfigContract::ClientPayloadQueueCapacity.key,
                              ConfigContract::ClientPayloadQueueCapacity.path, &clientPayloadQueueCapacity, &error) ||
                !ReadUnsigned(*workload, ConfigContract::WorkloadSeed.key, ConfigContract::WorkloadSeed.path,
                              &workloadSeed, &error) ||
                !ReadUnsigned(*workload, ConfigContract::ControlIntervalMilliseconds.key,
                              ConfigContract::ControlIntervalMilliseconds.path, &controlIntervalMilliseconds, &error) ||
                !ReadUnsigned(*workload, ConfigContract::BoostPercent.key, ConfigContract::BoostPercent.path,
                              &boostPercent, &error) ||
                !ReadUnsigned(*sampling, ConfigContract::SamplingIntervalMilliseconds.key,
                              ConfigContract::SamplingIntervalMilliseconds.path, &samplingIntervalMilliseconds,
                              &error) ||
                !ReadUnsigned(*lifecycle, ConfigContract::ShutdownTimeoutMilliseconds.key,
                              ConfigContract::ShutdownTimeoutMilliseconds.path, &shutdownTimeoutMilliseconds, &error))
            {
                return Failure(std::move(error));
            }
            if (clientPort > std::numeric_limits<std::uint16_t>::max())
            {
                return Failure(std::string{ConfigContract::ClientPort.path} + " is out of range");
            }
            if (!IsPowerOfTwo(clientEventQueueCapacity) || !IsPowerOfTwo(clientPayloadQueueCapacity))
            {
                return Failure("clients queue capacities must be powers of two greater than or equal to 2");
            }
            if (boostPercent > 100)
            {
                return Failure(std::string{ConfigContract::BoostPercent.path} + " must not exceed 100");
            }

            const std::filesystem::path configDirectory = absoluteConfigPath.parent_path();
            const std::filesystem::path resolvedExecutablePath = ResolvePath(configDirectory, executablePath);
            const std::filesystem::path resolvedServerConfigPath = ResolvePath(configDirectory, serverConfigPath);
            const std::filesystem::path resolvedRunsRoot = ResolvePath(configDirectory, runsRoot);
            if (!IsRegularFile(resolvedExecutablePath, &error) || !IsRegularFile(resolvedServerConfigPath, &error))
            {
                return Failure(std::move(error));
            }

            BenchmarkWorldHostControllerConfigResolveResult result;
            result.config.executablePath = Utf8String(resolvedExecutablePath);
            result.config.serverConfigPath = Utf8String(resolvedServerConfigPath);
            result.config.runsRoot = Utf8String(resolvedRunsRoot);
            result.config.clientAddress = clientAddress;
            result.config.clientPort = static_cast<std::uint16_t>(clientPort);
            result.config.clientCount = clientCount;
            result.config.clientRampPerSecond = clientRampPerSecond;
            result.config.clientAdmissionTimeoutMilliseconds = clientAdmissionTimeoutMilliseconds;
            result.config.clientRoundResultTimeoutMilliseconds = clientRoundResultTimeoutMilliseconds;
            result.config.clientEventQueueCapacity = clientEventQueueCapacity;
            result.config.clientPayloadQueueCapacity = clientPayloadQueueCapacity;
            result.config.workloadSeed = workloadSeed;
            result.config.controlIntervalMilliseconds = controlIntervalMilliseconds;
            result.config.boostPercent = boostPercent;
            result.config.samplingIntervalMilliseconds = samplingIntervalMilliseconds;
            result.config.shutdownTimeoutMilliseconds = shutdownTimeoutMilliseconds;

            JsonObject normalized = JsonObject::object();
            normalized[ConfigContract::Schema.key] = std::string{ConfigContract::SchemaName};
            normalized[ConfigContract::Version.key] = ConfigContract::SchemaVersion;
            normalized[ConfigContract::WorldHostSection][ConfigContract::ExecutablePath.key] =
                result.config.executablePath;
            normalized[ConfigContract::WorldHostSection][ConfigContract::ServerConfigPath.key] =
                result.config.serverConfigPath;
            normalized[ConfigContract::ArtifactSection][ConfigContract::RunsRoot.key] = result.config.runsRoot;
            normalized[ConfigContract::ClientsSection][ConfigContract::ClientAddress.key] = result.config.clientAddress;
            normalized[ConfigContract::ClientsSection][ConfigContract::ClientPort.key] = result.config.clientPort;
            normalized[ConfigContract::ClientsSection][ConfigContract::ClientCount.key] = result.config.clientCount;
            normalized[ConfigContract::ClientsSection][ConfigContract::ClientRampPerSecond.key] =
                result.config.clientRampPerSecond;
            normalized[ConfigContract::ClientsSection][ConfigContract::ClientAdmissionTimeoutMilliseconds.key] =
                result.config.clientAdmissionTimeoutMilliseconds;
            normalized[ConfigContract::ClientsSection][ConfigContract::ClientRoundResultTimeoutMilliseconds.key] =
                result.config.clientRoundResultTimeoutMilliseconds;
            normalized[ConfigContract::ClientsSection][ConfigContract::ClientEventQueueCapacity.key] =
                result.config.clientEventQueueCapacity;
            normalized[ConfigContract::ClientsSection][ConfigContract::ClientPayloadQueueCapacity.key] =
                result.config.clientPayloadQueueCapacity;
            normalized[ConfigContract::WorkloadSection][ConfigContract::WorkloadSeed.key] = result.config.workloadSeed;
            normalized[ConfigContract::WorkloadSection][ConfigContract::ControlIntervalMilliseconds.key] =
                result.config.controlIntervalMilliseconds;
            normalized[ConfigContract::WorkloadSection][ConfigContract::BoostPercent.key] = result.config.boostPercent;
            normalized[ConfigContract::SamplingSection][ConfigContract::SamplingIntervalMilliseconds.key] =
                samplingIntervalMilliseconds;
            normalized[ConfigContract::LifecycleSection][ConfigContract::ShutdownTimeoutMilliseconds.key] =
                shutdownTimeoutMilliseconds;
            result.normalizedJson = normalized.dump(2);
            return result;
        }
        catch (const std::exception& exception)
        {
            return Failure("World Host controller config resolution failed: " + std::string{exception.what()});
        }
        catch (...)
        {
            return Failure("World Host controller config resolution failed with an unknown error");
        }
    }
} // namespace psnr::benchmark
