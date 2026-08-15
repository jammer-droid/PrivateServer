#include "pch.h"

#include "ApplicationLogRecord.h"
#include "ApplicationLogValidation.h"

namespace psnr::logging
{
    bool ApplicationLogRecord::IsValid(const ApplicationLogRecord& record) noexcept
    {
        const bool hasEntityId = record.context.entityId.has_value();
        const bool hasEntityGeneration = record.context.entityGeneration.has_value();

        return internal::IsValidSeverity(record.severity) && internal::IsStableToken(record.component) &&
               internal::IsStableToken(record.event) &&
               (!record.message.has_value() ||
                (!record.message->empty() && internal::IsValidUtf8(*record.message))) &&
               internal::IsValidOptionalToken(record.context.operation) &&
               internal::IsValidOptionalToken(record.context.result) &&
               internal::IsValidOptionalToken(record.context.error) && hasEntityId == hasEntityGeneration;
    }
} // namespace psnr::logging
