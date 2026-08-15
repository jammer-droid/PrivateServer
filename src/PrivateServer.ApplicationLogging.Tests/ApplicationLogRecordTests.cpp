#include "pch.h"

#include "ApplicationLogRecord.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace psnr::logging
{
    namespace
    {
        [[nodiscard]] ApplicationLogRecord MakeValidRecord()
        {
            ApplicationLogRecord record{};
            record.severity = ApplicationLogSeverity::Warning;
            record.component = "world";
            record.event = "session_joined";
            return record;
        }
    } // namespace

    TEST(ApplicationLogRecordTests, DefaultsOptionalValuesToAbsent)
    {
        const ApplicationLogRecord record{};

        EXPECT_EQ(record.severity, ApplicationLogSeverity::Info);
        EXPECT_EQ(record.message, std::nullopt);
        EXPECT_EQ(record.context.serverTick, std::nullopt);
        EXPECT_EQ(record.context.epoch, std::nullopt);
        EXPECT_EQ(record.context.worldSessionKey, std::nullopt);
        EXPECT_EQ(record.context.entityId, std::nullopt);
        EXPECT_EQ(record.context.entityGeneration, std::nullopt);
        EXPECT_EQ(record.context.operation, std::nullopt);
        EXPECT_EQ(record.context.result, std::nullopt);
        EXPECT_EQ(record.context.error, std::nullopt);
        EXPECT_EQ(record.context.nativeErrorCode, std::nullopt);
    }

    TEST(ApplicationLogRecordTests, AcceptsStructuredRecordWithCorrelation)
    {
        ApplicationLogRecord record = MakeValidRecord();
        record.severity = ApplicationLogSeverity::Error;
        record.event = "session_cleanup_failed";
        record.message = "World session cleanup failed.";
        record.context.serverTick = 42;
        record.context.epoch = 3;
        record.context.worldSessionKey = 17;
        record.context.entityId = 91;
        record.context.entityGeneration = 2;
        record.context.operation = "session_cleanup";
        record.context.result = "failed";
        record.context.error = "native_close_failed";
        record.context.nativeErrorCode = 10'054;

        EXPECT_TRUE(ApplicationLogRecord::IsValid(record));
    }

    TEST(ApplicationLogRecordTests, RejectsMalformedStableTokens)
    {
        const std::array<std::string_view, 7> malformedTokens{
            "",
            "World",
            "_world",
            "world_",
            "world__session",
            "world-session",
            "2world",
        };

        for (const std::string_view malformedToken : malformedTokens)
        {
            ApplicationLogRecord malformedComponent = MakeValidRecord();
            malformedComponent.component = malformedToken;

            ApplicationLogRecord malformedEvent = MakeValidRecord();
            malformedEvent.event = malformedToken;

            EXPECT_FALSE(ApplicationLogRecord::IsValid(malformedComponent)) << malformedToken;
            EXPECT_FALSE(ApplicationLogRecord::IsValid(malformedEvent)) << malformedToken;
        }
    }

    TEST(ApplicationLogRecordTests, RejectsMalformedOptionalTokenAndEmptyMessage)
    {
        ApplicationLogRecord malformedOperation = MakeValidRecord();
        malformedOperation.context.operation = "SessionAdmission";

        ApplicationLogRecord malformedResult = MakeValidRecord();
        malformedResult.context.result = "not-accepted";

        ApplicationLogRecord malformedError = MakeValidRecord();
        malformedError.context.error = "native error";

        ApplicationLogRecord emptyMessage = MakeValidRecord();
        emptyMessage.message = "";

        EXPECT_FALSE(ApplicationLogRecord::IsValid(malformedOperation));
        EXPECT_FALSE(ApplicationLogRecord::IsValid(malformedResult));
        EXPECT_FALSE(ApplicationLogRecord::IsValid(malformedError));
        EXPECT_FALSE(ApplicationLogRecord::IsValid(emptyMessage));
    }

    TEST(ApplicationLogRecordTests, RequiresCompleteEntityIdentity)
    {
        ApplicationLogRecord missingGeneration = MakeValidRecord();
        missingGeneration.context.entityId = 91;

        ApplicationLogRecord missingEntityId = MakeValidRecord();
        missingEntityId.context.entityGeneration = 2;

        EXPECT_FALSE(ApplicationLogRecord::IsValid(missingGeneration));
        EXPECT_FALSE(ApplicationLogRecord::IsValid(missingEntityId));
    }

    TEST(ApplicationLogRecordTests, RejectsUnknownSeverity)
    {
        ApplicationLogRecord record = MakeValidRecord();
        record.severity = static_cast<ApplicationLogSeverity>(5);

        EXPECT_FALSE(ApplicationLogRecord::IsValid(record));
    }

    TEST(ApplicationLogRecordTests, RejectsInvalidUtf8Message)
    {
        const std::array<std::string, 5> invalidMessages{
            std::string{static_cast<char>(0x80)},
            std::string{static_cast<char>(0xC0), static_cast<char>(0xAF)},
            std::string{static_cast<char>(0xE2), static_cast<char>(0x82)},
            std::string{static_cast<char>(0xED), static_cast<char>(0xA0), static_cast<char>(0x80)},
            std::string{static_cast<char>(0xF4), static_cast<char>(0x90), static_cast<char>(0x80),
                        static_cast<char>(0x80)},
        };

        for (const std::string& invalidMessage : invalidMessages)
        {
            ApplicationLogRecord record = MakeValidRecord();
            record.message = invalidMessage;

            EXPECT_FALSE(ApplicationLogRecord::IsValid(record));
        }
    }

    TEST(ApplicationLogRecordTests, AcceptsValidUtf8Message)
    {
        ApplicationLogRecord record = MakeValidRecord();
        record.message = "월드 작업이 종료됨 😀";

        EXPECT_TRUE(ApplicationLogRecord::IsValid(record));
    }
} // namespace psnr::logging
