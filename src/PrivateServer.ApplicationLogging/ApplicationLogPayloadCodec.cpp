#include "pch.h"

#include "ApplicationLogPayloadCodec.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace psnr::logging::internal
{
    namespace
    {
        using ApplicationLogPayloadJson = nlohmann::ordered_json;

        constexpr std::string_view PayloadSchemaName = "ps.application.log.envelope";
        constexpr std::uint64_t PayloadSchemaVersion = 1;

        template <typename TValue>
        void AddOptionalProperty(ApplicationLogPayloadJson& document, const char* const name,
                                 const std::optional<TValue>& value)
        {
            if (value.has_value())
            {
                document[name] = *value;
            }
        }

        template <typename TValue>
        void ReadOptionalProperty(const ApplicationLogPayloadJson& document, const char* const name,
                                  std::optional<TValue>* const output)
        {
            if (document.contains(name))
            {
                *output = document.at(name).get<TValue>();
            }
            else
            {
                output->reset();
            }
        }
    } // namespace

    std::string ApplicationLogPayloadCodec::Encode(const ApplicationLogEnvelope& envelope)
    {
        const ApplicationLogRecord& record = envelope.record;
        const ApplicationLogContext& context = record.context;
        const std::int64_t timestampUnixMilliseconds =
            std::chrono::duration_cast<std::chrono::milliseconds>(envelope.producerTimestampUtc.time_since_epoch())
                .count();

        ApplicationLogPayloadJson document = ApplicationLogPayloadJson::object();
        document["schema"] = std::string(PayloadSchemaName);
        document["version"] = PayloadSchemaVersion;
        document["timestampUnixMilliseconds"] = timestampUnixMilliseconds;
        document["producerSteadyNs"] = envelope.producerSteadyNs;
        document["severity"] = static_cast<std::uint8_t>(record.severity);
        document["component"] = record.component;
        document["event"] = record.event;
        AddOptionalProperty(document, "message", record.message);
        document["messageTruncated"] = envelope.messageTruncated;
        AddOptionalProperty(document, "serverTick", context.serverTick);
        AddOptionalProperty(document, "epoch", context.epoch);
        AddOptionalProperty(document, "worldSessionKey", context.worldSessionKey);
        AddOptionalProperty(document, "entityId", context.entityId);
        AddOptionalProperty(document, "entityGeneration", context.entityGeneration);
        AddOptionalProperty(document, "operation", context.operation);
        AddOptionalProperty(document, "result", context.result);
        AddOptionalProperty(document, "error", context.error);
        AddOptionalProperty(document, "nativeErrorCode", context.nativeErrorCode);
        return document.dump();
    }

    bool ApplicationLogPayloadCodec::TryDecode(const std::string_view payload,
                                               ApplicationLogEnvelope* const output) noexcept
    {
        if (output == nullptr)
        {
            return false;
        }

        try
        {
            const ApplicationLogPayloadJson document = ApplicationLogPayloadJson::parse(payload.begin(), payload.end());
            if (!document.is_object() || document.at("schema").get<std::string>() != std::string(PayloadSchemaName) ||
                document.at("version").get<std::uint64_t>() != PayloadSchemaVersion)
            {
                return false;
            }

            ApplicationLogEnvelope prepared{};
            const std::int64_t timestampUnixMilliseconds = document.at("timestampUnixMilliseconds").get<std::int64_t>();
            prepared.producerTimestampUtc =
                std::chrono::system_clock::time_point{std::chrono::milliseconds{timestampUnixMilliseconds}};
            prepared.producerSteadyNs = document.at("producerSteadyNs").get<std::uint64_t>();

            const std::uint64_t severity = document.at("severity").get<std::uint64_t>();
            if (severity > static_cast<std::uint64_t>(ApplicationLogSeverity::Critical))
            {
                return false;
            }

            prepared.record.severity = static_cast<ApplicationLogSeverity>(severity);
            prepared.record.component = document.at("component").get<std::string>();
            prepared.record.event = document.at("event").get<std::string>();
            ReadOptionalProperty(document, "message", &prepared.record.message);
            prepared.messageTruncated = document.at("messageTruncated").get<bool>();

            ApplicationLogContext& context = prepared.record.context;
            ReadOptionalProperty(document, "serverTick", &context.serverTick);
            ReadOptionalProperty(document, "epoch", &context.epoch);
            ReadOptionalProperty(document, "worldSessionKey", &context.worldSessionKey);
            ReadOptionalProperty(document, "entityId", &context.entityId);
            ReadOptionalProperty(document, "entityGeneration", &context.entityGeneration);
            ReadOptionalProperty(document, "operation", &context.operation);
            ReadOptionalProperty(document, "result", &context.result);
            ReadOptionalProperty(document, "error", &context.error);
            ReadOptionalProperty(document, "nativeErrorCode", &context.nativeErrorCode);

            if (!ApplicationLogRecord::IsValid(prepared.record) ||
                (prepared.record.message.has_value() &&
                 prepared.record.message->size() > MaximumApplicationLogMessageBytes) ||
                (prepared.messageTruncated && !prepared.record.message.has_value()))
            {
                return false;
            }

            *output = std::move(prepared);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }
} // namespace psnr::logging::internal
