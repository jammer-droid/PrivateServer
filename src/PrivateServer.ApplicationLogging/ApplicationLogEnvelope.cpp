#include "pch.h"

#include "ApplicationLogEnvelope.h"

#include "ApplicationLogValidation.h"

#include <cstddef>
#include <utility>

namespace psnr::logging::internal
{
    bool ApplicationLogEnvelope::TryCreate(const ApplicationLogRecord& record,
                                           const std::chrono::system_clock::time_point producerTimestampUtc,
                                           const std::uint64_t producerSteadyNs, ApplicationLogEnvelope* const output)
    {
        if (output == nullptr || !ApplicationLogRecord::IsValid(record))
        {
            return false;
        }

        ApplicationLogEnvelope prepared{};
        prepared.producerTimestampUtc = producerTimestampUtc;
        prepared.producerSteadyNs = producerSteadyNs;
        prepared.record = record;

        if (prepared.record.message.has_value() && prepared.record.message->size() > MaximumApplicationLogMessageBytes)
        {
            const std::size_t truncatedSize =
                Utf8PrefixSizeWithinLimit(*prepared.record.message, MaximumApplicationLogMessageBytes);
            prepared.record.message->resize(truncatedSize);
            prepared.messageTruncated = true;
        }

        *output = std::move(prepared);
        return true;
    }
} // namespace psnr::logging::internal
