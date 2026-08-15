#include "pch.h"

#include "ApplicationLogPayloadCodec.h"

#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace psnr::logging::internal
{
    namespace
    {
        using ApplicationLogPayloadJson = nlohmann::ordered_json;

        [[nodiscard]] ApplicationLogRecord MakeRecord()
        {
            ApplicationLogRecord record{};
            record.severity = ApplicationLogSeverity::Error;
            record.component = "world";
            record.event = "session_cleanup_failed";
            record.message = std::string(MaximumApplicationLogMessageBytes + 1, 'a');
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

        [[nodiscard]] ApplicationLogEnvelope MakeEnvelope()
        {
            const ApplicationLogRecord record = MakeRecord();
            ApplicationLogEnvelope envelope{};
            const bool created = ApplicationLogEnvelope::TryCreate(
                record,
                std::chrono::system_clock::time_point{std::chrono::milliseconds{1'785'598'212'482}},
                123'456, &envelope);
            EXPECT_TRUE(created);
            return envelope;
        }
    } // namespace

    TEST(ApplicationLogPayloadCodecTests, RoundTripsOwnedEnvelope)
    {
        const ApplicationLogEnvelope source = MakeEnvelope();

        const std::string payload = ApplicationLogPayloadCodec::Encode(source);
        ApplicationLogEnvelope decoded{};
        ASSERT_TRUE(ApplicationLogPayloadCodec::TryDecode(payload, &decoded));

        EXPECT_EQ(payload.find('\n'), std::string::npos);
        EXPECT_EQ(decoded.producerTimestampUtc, source.producerTimestampUtc);
        EXPECT_EQ(decoded.producerSteadyNs, source.producerSteadyNs);
        EXPECT_EQ(decoded.record.severity, source.record.severity);
        EXPECT_EQ(decoded.record.component, source.record.component);
        EXPECT_EQ(decoded.record.event, source.record.event);
        EXPECT_EQ(decoded.record.message, source.record.message);
        EXPECT_EQ(decoded.messageTruncated, source.messageTruncated);
        EXPECT_EQ(decoded.record.context.serverTick, source.record.context.serverTick);
        EXPECT_EQ(decoded.record.context.epoch, source.record.context.epoch);
        EXPECT_EQ(decoded.record.context.worldSessionKey, source.record.context.worldSessionKey);
        EXPECT_EQ(decoded.record.context.entityId, source.record.context.entityId);
        EXPECT_EQ(decoded.record.context.entityGeneration, source.record.context.entityGeneration);
        EXPECT_EQ(decoded.record.context.operation, source.record.context.operation);
        EXPECT_EQ(decoded.record.context.result, source.record.context.result);
        EXPECT_EQ(decoded.record.context.error, source.record.context.error);
        EXPECT_EQ(decoded.record.context.nativeErrorCode, source.record.context.nativeErrorCode);
    }

    TEST(ApplicationLogPayloadCodecTests, RejectsMalformedPayloadWithoutModifyingOutput)
    {
        const std::array<std::string_view, 4> malformedPayloads{
            "",
            "[]",
            "{}",
            R"({"schema":"wrong","version":1})",
        };

        for (const std::string_view malformedPayload : malformedPayloads)
        {
            ApplicationLogEnvelope output{};
            output.producerSteadyNs = 777;

            EXPECT_FALSE(ApplicationLogPayloadCodec::TryDecode(malformedPayload, &output));
            EXPECT_EQ(output.producerSteadyNs, 777);
        }
    }

    TEST(ApplicationLogPayloadCodecTests, RejectsPayloadThatBypassesMessageBound)
    {
        const ApplicationLogEnvelope source = MakeEnvelope();
        ApplicationLogPayloadJson document =
            ApplicationLogPayloadJson::parse(ApplicationLogPayloadCodec::Encode(source));
        document["message"] = std::string(MaximumApplicationLogMessageBytes + 1, 'a');

        ApplicationLogEnvelope output{};

        EXPECT_FALSE(ApplicationLogPayloadCodec::TryDecode(document.dump(), &output));
    }

    TEST(ApplicationLogPayloadCodecTests, RejectsTruncatedFlagWithoutMessage)
    {
        const ApplicationLogEnvelope source = MakeEnvelope();
        ApplicationLogPayloadJson document =
            ApplicationLogPayloadJson::parse(ApplicationLogPayloadCodec::Encode(source));
        document.erase("message");
        document["messageTruncated"] = true;

        ApplicationLogEnvelope output{};

        EXPECT_FALSE(ApplicationLogPayloadCodec::TryDecode(document.dump(), &output));
    }

    TEST(ApplicationLogPayloadCodecTests, RejectsUnknownSeverityAndNullOutput)
    {
        const ApplicationLogEnvelope source = MakeEnvelope();
        ApplicationLogPayloadJson document =
            ApplicationLogPayloadJson::parse(ApplicationLogPayloadCodec::Encode(source));
        document["severity"] = 5;
        const std::string payload = document.dump();

        ApplicationLogEnvelope output{};

        EXPECT_FALSE(ApplicationLogPayloadCodec::TryDecode(payload, &output));
        EXPECT_FALSE(ApplicationLogPayloadCodec::TryDecode(payload, nullptr));
    }
} // namespace psnr::logging::internal
