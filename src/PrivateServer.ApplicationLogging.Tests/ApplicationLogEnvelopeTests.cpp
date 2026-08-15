#include "pch.h"

#include "ApplicationLogEnvelope.h"

#include <chrono>
#include <string>

namespace psnr::logging::internal
{
    namespace
    {
        [[nodiscard]] ApplicationLogRecord MakeValidRecord()
        {
            ApplicationLogRecord record{};
            record.component = "world";
            record.event = "worker_stopped";
            record.message = "worker stopped";
            return record;
        }

        [[nodiscard]] std::chrono::system_clock::time_point ProducerTimestamp()
        {
            return std::chrono::system_clock::time_point{std::chrono::milliseconds{1'785'598'212'482}};
        }
    } // namespace

    TEST(ApplicationLogEnvelopeTests, OwnsRecordAndProducerTimestamps)
    {
        ApplicationLogRecord record = MakeValidRecord();
        ApplicationLogEnvelope envelope{};

        ASSERT_TRUE(ApplicationLogEnvelope::TryCreate(record, ProducerTimestamp(), 123'456, &envelope));
        record.component = "changed";
        record.message = "changed";

        EXPECT_EQ(envelope.producerTimestampUtc, ProducerTimestamp());
        EXPECT_EQ(envelope.producerSteadyNs, 123'456);
        EXPECT_EQ(envelope.record.component, "world");
        ASSERT_TRUE(envelope.record.message.has_value());
        EXPECT_EQ(*envelope.record.message, "worker stopped");
        EXPECT_FALSE(envelope.messageTruncated);
    }

    TEST(ApplicationLogEnvelopeTests, KeepsMessageAtByteLimit)
    {
        ApplicationLogRecord record = MakeValidRecord();
        record.message = std::string(MaximumApplicationLogMessageBytes, 'a');
        ApplicationLogEnvelope envelope{};

        ASSERT_TRUE(ApplicationLogEnvelope::TryCreate(record, ProducerTimestamp(), 1, &envelope));

        ASSERT_TRUE(envelope.record.message.has_value());
        EXPECT_EQ(envelope.record.message->size(), MaximumApplicationLogMessageBytes);
        EXPECT_FALSE(envelope.messageTruncated);
    }

    TEST(ApplicationLogEnvelopeTests, TruncatesAsciiMessageToByteLimit)
    {
        ApplicationLogRecord record = MakeValidRecord();
        record.message = std::string(MaximumApplicationLogMessageBytes + 1, 'a');
        ApplicationLogEnvelope envelope{};

        ASSERT_TRUE(ApplicationLogEnvelope::TryCreate(record, ProducerTimestamp(), 1, &envelope));

        ASSERT_TRUE(envelope.record.message.has_value());
        EXPECT_EQ(envelope.record.message->size(), MaximumApplicationLogMessageBytes);
        EXPECT_TRUE(envelope.messageTruncated);
    }

    TEST(ApplicationLogEnvelopeTests, DoesNotSplitUtf8CodePointAtByteLimit)
    {
        ApplicationLogRecord record = MakeValidRecord();
        record.message = std::string(MaximumApplicationLogMessageBytes - 1, 'a');
        record.message->append("가");
        ApplicationLogEnvelope envelope{};

        ASSERT_TRUE(ApplicationLogEnvelope::TryCreate(record, ProducerTimestamp(), 1, &envelope));

        ASSERT_TRUE(envelope.record.message.has_value());
        EXPECT_EQ(envelope.record.message->size(), MaximumApplicationLogMessageBytes - 1);
        EXPECT_EQ(*envelope.record.message, std::string(MaximumApplicationLogMessageBytes - 1, 'a'));
        EXPECT_TRUE(envelope.messageTruncated);
    }

    TEST(ApplicationLogEnvelopeTests, RejectsInvalidUtf8WithoutModifyingOutput)
    {
        ApplicationLogRecord record = MakeValidRecord();
        std::string invalidMessage;
        invalidMessage.push_back(static_cast<char>(0xC3));
        invalidMessage.push_back('(');
        record.message = invalidMessage;

        ApplicationLogEnvelope envelope{};
        envelope.producerSteadyNs = 777;

        EXPECT_FALSE(ApplicationLogEnvelope::TryCreate(record, ProducerTimestamp(), 1, &envelope));
        EXPECT_EQ(envelope.producerSteadyNs, 777);
    }

    TEST(ApplicationLogEnvelopeTests, RejectsNullOutput)
    {
        const ApplicationLogRecord record = MakeValidRecord();

        EXPECT_FALSE(ApplicationLogEnvelope::TryCreate(record, ProducerTimestamp(), 1, nullptr));
    }
} // namespace psnr::logging::internal
