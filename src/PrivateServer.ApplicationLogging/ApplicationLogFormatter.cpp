#include "pch.h"

#include "ApplicationLogFormatter.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include <string_view>

namespace psnr::logging::internal
{
    namespace
    {
        using ApplicationLogJson = nlohmann::ordered_json;

        constexpr std::string_view SchemaName = "ps.application.log";
        constexpr std::uint64_t SchemaVersion = 1;

        [[nodiscard]] std::string_view SeverityName(const ApplicationLogSeverity severity) noexcept
        {
            switch (severity)
            {
            case ApplicationLogSeverity::Debug:
                return "debug";
            case ApplicationLogSeverity::Info:
                return "info";
            case ApplicationLogSeverity::Warning:
                return "warning";
            case ApplicationLogSeverity::Error:
                return "error";
            case ApplicationLogSeverity::Critical:
                return "critical";
            default:
                return "unknown";
            }
        }

        [[nodiscard]] std::string FormatTimestampUtc(const std::chrono::system_clock::time_point timestampUtc)
        {
            const std::chrono::system_clock::time_point secondPrecision =
                std::chrono::time_point_cast<std::chrono::seconds>(timestampUtc);
            const std::int64_t milliseconds =
                std::chrono::duration_cast<std::chrono::milliseconds>(timestampUtc - secondPrecision).count();
            const std::time_t rawTime = std::chrono::system_clock::to_time_t(secondPrecision);
            std::tm utcCalendar{};

            if (gmtime_s(&utcCalendar, &rawTime) != 0)
            {
                return {};
            }

            char buffer[25]{};
            const int written = std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d.%03lldZ",
                                              utcCalendar.tm_year + 1900, utcCalendar.tm_mon + 1, utcCalendar.tm_mday,
                                              utcCalendar.tm_hour, utcCalendar.tm_min, utcCalendar.tm_sec,
                                              static_cast<long long>(milliseconds));
            if (written != 24)
            {
                return {};
            }

            return std::string{buffer, 24};
        }

        void AppendConsoleUnsigned(const std::string_view name, const std::uint64_t value, std::string* const output)
        {
            output->push_back(' ');
            output->append(name);
            output->push_back('=');
            output->append(std::to_string(value));
        }

        void AppendConsoleToken(const std::string_view name, const std::string_view value, std::string* const output)
        {
            output->push_back(' ');
            output->append(name);
            output->push_back('=');
            output->append(value);
        }
    } // namespace

    std::string ApplicationLogFormatter::FormatJsonPayload(const ApplicationLogFormatInput& input)
    {
        const ApplicationLogRecord& record = input.envelope.record;
        ApplicationLogJson document = ApplicationLogJson::object();
        document["schema"] = std::string(SchemaName);
        document["version"] = SchemaVersion;
        document["timestampUtc"] = FormatTimestampUtc(input.envelope.producerTimestampUtc);
        document["producerSteadyNs"] = input.envelope.producerSteadyNs;
        document["drainSequence"] = input.drainSequence;
        document["runId"] = input.config.runId;
        document["process"] = input.config.process;
        document["severity"] = std::string(SeverityName(record.severity));
        document["component"] = record.component;
        document["event"] = record.event;

        if (record.message.has_value())
        {
            document["message"] = *record.message;
        }

        document["messageTruncated"] = input.envelope.messageTruncated;

        const ApplicationLogContext& context = record.context;
        if (context.serverTick.has_value())
        {
            document["serverTick"] = *context.serverTick;
        }
        if (context.epoch.has_value())
        {
            document["epoch"] = *context.epoch;
        }
        if (context.worldSessionKey.has_value())
        {
            document["worldSessionKey"] = *context.worldSessionKey;
        }
        if (context.entityId.has_value())
        {
            document["entityId"] = *context.entityId;
        }
        if (context.entityGeneration.has_value())
        {
            document["entityGeneration"] = *context.entityGeneration;
        }
        if (context.operation.has_value())
        {
            document["operation"] = *context.operation;
        }
        if (context.result.has_value())
        {
            document["result"] = *context.result;
        }
        if (context.error.has_value())
        {
            document["error"] = *context.error;
        }
        if (context.nativeErrorCode.has_value())
        {
            document["nativeErrorCode"] = *context.nativeErrorCode;
        }

        return document.dump();
    }

    std::string ApplicationLogFormatter::FormatConsolePayload(const ApplicationLogFormatInput& input)
    {
        const ApplicationLogRecord& record = input.envelope.record;
        std::string output = FormatTimestampUtc(input.envelope.producerTimestampUtc);
        output.reserve(256);
        output.append(" [");
        output.append(SeverityName(record.severity));
        output.append("] component=");
        output.append(record.component);
        output.append(" event=");
        output.append(record.event);

        const ApplicationLogContext& context = record.context;
        if (context.serverTick.has_value())
        {
            AppendConsoleUnsigned("tick", *context.serverTick, &output);
        }
        if (context.epoch.has_value())
        {
            AppendConsoleUnsigned("epoch", *context.epoch, &output);
        }
        if (context.worldSessionKey.has_value())
        {
            AppendConsoleUnsigned("session", *context.worldSessionKey, &output);
        }
        if (context.entityId.has_value() && context.entityGeneration.has_value())
        {
            output.append(" entity=");
            output.append(std::to_string(*context.entityId));
            output.push_back(':');
            output.append(std::to_string(*context.entityGeneration));
        }
        if (context.operation.has_value())
        {
            AppendConsoleToken("operation", *context.operation, &output);
        }
        if (context.result.has_value())
        {
            AppendConsoleToken("result", *context.result, &output);
        }
        if (context.error.has_value())
        {
            AppendConsoleToken("error", *context.error, &output);
        }
        if (context.nativeErrorCode.has_value())
        {
            AppendConsoleUnsigned("nativeError", *context.nativeErrorCode, &output);
        }
        if (record.message.has_value())
        {
            output.append(" message=");
            output.append(ApplicationLogJson(*record.message).dump());
        }
        if (input.envelope.messageTruncated)
        {
            output.append(" messageTruncated=true");
        }

        return output;
    }
} // namespace psnr::logging::internal
