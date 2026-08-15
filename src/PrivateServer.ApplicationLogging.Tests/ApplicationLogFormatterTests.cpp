#include "pch.h"

#include "ApplicationLogFormatter.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <string>

namespace psnr::logging::internal
{
    namespace
    {
        using ApplicationLogJson = nlohmann::json;

        [[nodiscard]] ApplicationLogConfig MakeConfig()
        {
            ApplicationLogConfig config{};
            config.runId = "run-20260801T153012.482Z-550e8400-e29b-41d4-a716-446655440000";
            config.process = "world-server-host";
            config.outputDirectory = "artifacts/runs/test/world";
            return config;
        }

        [[nodiscard]] ApplicationLogRecord MakeRecord()
        {
            ApplicationLogRecord record{};
            record.severity = ApplicationLogSeverity::Error;
            record.component = "world";
            record.event = "session_cleanup_failed";
            record.message = "cleanup failed";
            record.context.serverTick = 42;
            record.context.epoch = 3;
            record.context.worldSessionKey = 17;
            record.context.entityId = 91;
            record.context.entityGeneration = 2;
            record.context.operation = "session_cleanup";
            record.context.result = "failed";
            record.context.error = "native_close_failed";
            record.context.nativeErrorCode = 10'054;
            return record;
        }

        [[nodiscard]] ApplicationLogEnvelope MakeEnvelope(const ApplicationLogRecord& record)
        {
            ApplicationLogEnvelope envelope{};
            const bool created = ApplicationLogEnvelope::TryCreate(
                record,
                std::chrono::system_clock::time_point{std::chrono::milliseconds{1'785'598'212'482}},
                123'456, &envelope);
            EXPECT_TRUE(created);
            return envelope;
        }

        [[nodiscard]] ApplicationLogFormatInput MakeInput(const ApplicationLogConfig& config,
                                                          const ApplicationLogEnvelope& envelope)
        {
            ApplicationLogFormatInput input{config, envelope};
            input.drainSequence = 7;
            return input;
        }
    } // namespace

    TEST(ApplicationLogFormatterTests, FormatsCompleteJsonPayload)
    {
        const ApplicationLogConfig config = MakeConfig();
        const ApplicationLogRecord record = MakeRecord();
        const ApplicationLogEnvelope envelope = MakeEnvelope(record);
        const ApplicationLogFormatInput input = MakeInput(config, envelope);

        const std::string payload = ApplicationLogFormatter::FormatJsonPayload(input);
        const ApplicationLogJson document = ApplicationLogJson::parse(payload);

        ASSERT_TRUE(document.is_object());
        EXPECT_EQ(document.size(), 21);
        EXPECT_EQ(document.at("schema").get<std::string>(), "ps.application.log");
        EXPECT_EQ(document.at("version").get<std::uint64_t>(), 1);
        EXPECT_EQ(document.at("timestampUtc").get<std::string>(), "2026-08-01T15:30:12.482Z");
        EXPECT_EQ(document.at("producerSteadyNs").get<std::uint64_t>(), 123'456);
        EXPECT_EQ(document.at("drainSequence").get<std::uint64_t>(), 7);
        EXPECT_EQ(document.at("runId").get<std::string>(), config.runId);
        EXPECT_EQ(document.at("process").get<std::string>(), config.process);
        EXPECT_EQ(document.at("severity").get<std::string>(), "error");
        EXPECT_EQ(document.at("component").get<std::string>(), "world");
        EXPECT_EQ(document.at("event").get<std::string>(), "session_cleanup_failed");
        EXPECT_EQ(document.at("message").get<std::string>(), "cleanup failed");
        EXPECT_FALSE(document.at("messageTruncated").get<bool>());
        EXPECT_EQ(document.at("serverTick").get<std::uint64_t>(), 42);
        EXPECT_EQ(document.at("epoch").get<std::uint64_t>(), 3);
        EXPECT_EQ(document.at("worldSessionKey").get<std::uint64_t>(), 17);
        EXPECT_EQ(document.at("entityId").get<std::uint32_t>(), 91);
        EXPECT_EQ(document.at("entityGeneration").get<std::uint32_t>(), 2);
        EXPECT_EQ(document.at("operation").get<std::string>(), "session_cleanup");
        EXPECT_EQ(document.at("result").get<std::string>(), "failed");
        EXPECT_EQ(document.at("error").get<std::string>(), "native_close_failed");
        EXPECT_EQ(document.at("nativeErrorCode").get<std::uint32_t>(), 10'054);
    }

    TEST(ApplicationLogFormatterTests, FormatsCompleteConsolePayload)
    {
        const ApplicationLogConfig config = MakeConfig();
        const ApplicationLogRecord record = MakeRecord();
        const ApplicationLogEnvelope envelope = MakeEnvelope(record);
        const ApplicationLogFormatInput input = MakeInput(config, envelope);

        const std::string payload = ApplicationLogFormatter::FormatConsolePayload(input);

        EXPECT_EQ(payload,
                  "2026-08-01T15:30:12.482Z [error] component=world event=session_cleanup_failed "
                  "tick=42 epoch=3 session=17 entity=91:2 operation=session_cleanup result=failed "
                  "error=native_close_failed nativeError=10054 message=\"cleanup failed\"");
    }

    TEST(ApplicationLogFormatterTests, OmitsAbsentOptionalContext)
    {
        const ApplicationLogConfig config = MakeConfig();
        ApplicationLogRecord record{};
        record.component = "host";
        record.event = "listen_ready";
        const ApplicationLogEnvelope envelope = MakeEnvelope(record);
        const ApplicationLogFormatInput input = MakeInput(config, envelope);

        const std::string jsonPayload = ApplicationLogFormatter::FormatJsonPayload(input);
        const std::string consolePayload = ApplicationLogFormatter::FormatConsolePayload(input);
        const ApplicationLogJson document = ApplicationLogJson::parse(jsonPayload);

        EXPECT_FALSE(document.contains("message"));
        EXPECT_FALSE(document.contains("serverTick"));
        EXPECT_FALSE(document.contains("worldSessionKey"));
        EXPECT_FALSE(document.contains("operation"));
        EXPECT_EQ(consolePayload.find("message="), std::string::npos);
        EXPECT_EQ(consolePayload.find("tick="), std::string::npos);
        EXPECT_EQ(consolePayload.find("session="), std::string::npos);
    }

    TEST(ApplicationLogFormatterTests, EscapesMessageAndReportsTruncation)
    {
        const ApplicationLogConfig config = MakeConfig();
        ApplicationLogRecord record = MakeRecord();
        record.message = "line1\n\"quoted\"\\tail\t";
        record.message->append(MaximumApplicationLogMessageBytes, 'a');
        const ApplicationLogEnvelope envelope = MakeEnvelope(record);
        const ApplicationLogFormatInput input = MakeInput(config, envelope);

        const std::string jsonPayload = ApplicationLogFormatter::FormatJsonPayload(input);
        const std::string consolePayload = ApplicationLogFormatter::FormatConsolePayload(input);
        const ApplicationLogJson document = ApplicationLogJson::parse(jsonPayload);

        ASSERT_TRUE(envelope.record.message.has_value());
        EXPECT_TRUE(envelope.messageTruncated);
        EXPECT_EQ(document.at("message").get<std::string>(), *envelope.record.message);
        EXPECT_TRUE(document.at("messageTruncated").get<bool>());
        EXPECT_NE(consolePayload.find("message=\"line1\\n\\\"quoted\\\"\\\\tail\\t"), std::string::npos);
        EXPECT_NE(consolePayload.find("messageTruncated=true"), std::string::npos);
        EXPECT_EQ(jsonPayload.find('\n'), std::string::npos);
        EXPECT_EQ(consolePayload.find('\n'), std::string::npos);
    }
} // namespace psnr::logging::internal
