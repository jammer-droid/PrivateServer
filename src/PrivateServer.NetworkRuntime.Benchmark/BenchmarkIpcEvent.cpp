#include "BenchmarkIpcEvent.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace psnr::benchmark
{
    namespace
    {
        constexpr std::string_view IpcSchemaName = "psnr.network_runtime.benchmark.control";
        constexpr std::uint64_t IpcSchemaVersion = 1;
        constexpr std::size_t MaximumRunIdBytes = 128;
        constexpr std::size_t MaximumErrorMessageBytes = 1024;

        constexpr std::string_view JsonKeySchema = "schema";
        constexpr std::string_view JsonKeyVersion = "version";
        constexpr std::string_view JsonKeyType = "type";
        constexpr std::string_view JsonKeyRunId = "runId";
        constexpr std::string_view JsonKeySequence = "sequence";
        constexpr std::string_view JsonKeyRuntimeSample = "runtimeSample";
        constexpr std::string_view JsonKeyErrorMessage = "errorMessage";
        constexpr std::string_view JsonKeyLifecycleState = "lifecycleState";
        constexpr std::string_view JsonKeyRegisteredSessionCount = "registeredSessionCount";
        constexpr std::string_view JsonKeyClosingSessionCount = "closingSessionCount";
        constexpr std::string_view JsonKeyPendingRecvIoCount = "pendingRecvIoCount";
        constexpr std::string_view JsonKeyPendingSendIoCount = "pendingSendIoCount";
        constexpr std::string_view JsonKeyToWorldEventDepth = "toWorldEventDepth";
        constexpr std::string_view JsonKeyToWorldEventHighWatermark = "toWorldEventHighWatermark";
        constexpr std::string_view JsonKeyPressureTransactionCounts = "pressureTransactionCounts";
        constexpr std::string_view JsonKeyPoolExhaustionCounts = "poolExhaustionCounts";
        constexpr std::string_view JsonKeyMemoryPools = "memoryPools";
        constexpr std::string_view JsonKeyTotalPressureTransactions = "totalPressureTransactions";
        constexpr std::string_view JsonKeyDiagnostics = "diagnostics";
        constexpr std::string_view JsonKeyCapacity = "capacity";
        constexpr std::string_view JsonKeyInUse = "inUse";
        constexpr std::string_view JsonKeyAvailable = "available";
        constexpr std::string_view JsonKeyHighWatermark = "highWatermark";
        constexpr std::string_view JsonKeyEnabled = "enabled";
        constexpr std::string_view JsonKeySinkFailed = "sinkFailed";
        constexpr std::string_view JsonKeyAttempted = "attempted";
        constexpr std::string_view JsonKeyEnqueued = "enqueued";
        constexpr std::string_view JsonKeyDroppedQueueFull = "droppedQueueFull";
        constexpr std::string_view JsonKeyDroppedSinkUnavailable = "droppedSinkUnavailable";
        constexpr std::string_view JsonKeyConsumed = "consumed";
        constexpr std::string_view JsonKeyDiscardedAfterSinkFailure = "discardedAfterSinkFailure";

        constexpr std::string_view ReadyType = "ready";
        constexpr std::string_view RuntimeSampleType = "runtimeSample";
        constexpr std::string_view ErrorType = "error";
        constexpr std::string_view StoppedType = "stopped";
        constexpr std::string_view CreatedLifecycleState = "created";
        constexpr std::string_view RunningLifecycleState = "running";
        constexpr std::string_view StopRequestedLifecycleState = "stopRequested";
        constexpr std::string_view ShutdownLifecycleState = "shutdown";

        using JsonObject = nlohmann::ordered_json;

        [[nodiscard]] BenchmarkIpcEventEncodeResult EncodeFailure(std::string error)
        {
            BenchmarkIpcEventEncodeResult result;
            result.error = std::move(error);
            return result;
        }

        [[nodiscard]] BenchmarkIpcEventDecodeResult DecodeFailure(std::string error)
        {
            BenchmarkIpcEventDecodeResult result;
            result.error = std::move(error);
            return result;
        }

        [[nodiscard]] const JsonObject* FindProperty(const JsonObject& document, const std::string_view key)
        {
            const JsonObject::const_iterator iterator = document.find(key);
            return iterator == document.end() ? nullptr : &(*iterator);
        }

        [[nodiscard]] std::string_view TypeToken(const BenchmarkIpcEventType type) noexcept
        {
            switch (type)
            {
            case BenchmarkIpcEventType::Ready:
                return ReadyType;
            case BenchmarkIpcEventType::RuntimeSample:
                return RuntimeSampleType;
            case BenchmarkIpcEventType::Error:
                return ErrorType;
            case BenchmarkIpcEventType::Stopped:
                return StoppedType;
            }

            return {};
        }

        [[nodiscard]] bool ParseType(const std::string_view token, BenchmarkIpcEventType* const outType) noexcept
        {
            if (outType == nullptr)
            {
                return false;
            }
            if (token == ReadyType)
            {
                *outType = BenchmarkIpcEventType::Ready;
                return true;
            }
            if (token == RuntimeSampleType)
            {
                *outType = BenchmarkIpcEventType::RuntimeSample;
                return true;
            }
            if (token == ErrorType)
            {
                *outType = BenchmarkIpcEventType::Error;
                return true;
            }
            if (token == StoppedType)
            {
                *outType = BenchmarkIpcEventType::Stopped;
                return true;
            }
            return false;
        }

        [[nodiscard]] std::string_view LifecycleStateToken(const psnr::runtime::NrServerLifecycleState state) noexcept
        {
            switch (state)
            {
            case psnr::runtime::NrServerLifecycleState::Created:
                return CreatedLifecycleState;
            case psnr::runtime::NrServerLifecycleState::Running:
                return RunningLifecycleState;
            case psnr::runtime::NrServerLifecycleState::StopRequested:
                return StopRequestedLifecycleState;
            case psnr::runtime::NrServerLifecycleState::Shutdown:
                return ShutdownLifecycleState;
            case psnr::runtime::NrServerLifecycleState::Invalid:
                break;
            }

            return {};
        }

        [[nodiscard]] bool ParseLifecycleState(const std::string_view token,
                                               psnr::runtime::NrServerLifecycleState* const outState) noexcept
        {
            if (outState == nullptr)
            {
                return false;
            }
            if (token == CreatedLifecycleState)
            {
                *outState = psnr::runtime::NrServerLifecycleState::Created;
                return true;
            }
            if (token == RunningLifecycleState)
            {
                *outState = psnr::runtime::NrServerLifecycleState::Running;
                return true;
            }
            if (token == StopRequestedLifecycleState)
            {
                *outState = psnr::runtime::NrServerLifecycleState::StopRequested;
                return true;
            }
            if (token == ShutdownLifecycleState)
            {
                *outState = psnr::runtime::NrServerLifecycleState::Shutdown;
                return true;
            }
            return false;
        }

        [[nodiscard]] bool ReadUnsigned(const JsonObject& document, const std::string_view key,
                                        std::uint64_t* const outValue)
        {
            const JsonObject* value = FindProperty(document, key);
            if (value == nullptr || !value->is_number_unsigned() || outValue == nullptr)
            {
                return false;
            }
            *outValue = value->get<std::uint64_t>();
            return true;
        }

        [[nodiscard]] bool ReadBoolean(const JsonObject& document, const std::string_view key, bool* const outValue)
        {
            const JsonObject* value = FindProperty(document, key);
            if (value == nullptr || !value->is_boolean() || outValue == nullptr)
            {
                return false;
            }
            *outValue = value->get<bool>();
            return true;
        }

        [[nodiscard]] std::string ValidateEnvelope(const BenchmarkIpcEventV1& event)
        {
            if (event.runId.empty() || event.runId.size() > MaximumRunIdBytes)
            {
                return "runId must contain between 1 and 128 bytes";
            }

            switch (event.type)
            {
            case BenchmarkIpcEventType::Ready:
                if (event.sequence != 0)
                {
                    return "ready event sequence must be zero";
                }
                break;
            case BenchmarkIpcEventType::RuntimeSample:
                if (event.sequence == 0)
                {
                    return "runtimeSample event sequence must be greater than zero";
                }
                if (LifecycleStateToken(event.runtimeSample.lifecycleState).empty())
                {
                    return "runtimeSample lifecycle state is invalid";
                }
                break;
            case BenchmarkIpcEventType::Error:
                if (event.errorMessage.empty() || event.errorMessage.size() > MaximumErrorMessageBytes)
                {
                    return "errorMessage must contain between 1 and 1024 bytes";
                }
                break;
            case BenchmarkIpcEventType::Stopped:
                if (event.sequence == 0)
                {
                    return "stopped event sequence must be greater than zero";
                }
                break;
            default:
                return "event type is unsupported";
            }

            return {};
        }

        [[nodiscard]] JsonObject EncodeRuntimeSample(const BenchmarkRuntimeSampleV1& sample)
        {
            JsonObject document = JsonObject::object();
            document[JsonKeyLifecycleState] = std::string(LifecycleStateToken(sample.lifecycleState));
            document[JsonKeyRegisteredSessionCount] = sample.registeredSessionCount;
            document[JsonKeyClosingSessionCount] = sample.closingSessionCount;
            document[JsonKeyPendingRecvIoCount] = sample.pendingRecvIoCount;
            document[JsonKeyPendingSendIoCount] = sample.pendingSendIoCount;
            document[JsonKeyToWorldEventDepth] = sample.toWorldEventDepth;
            document[JsonKeyToWorldEventHighWatermark] = sample.toWorldEventHighWatermark;
            document[JsonKeyTotalPressureTransactions] = sample.totalPressureTransactions;

            JsonObject pressureCounts = JsonObject::array();
            for (const std::uint64_t count : sample.pressureTransactionCounts)
            {
                pressureCounts.push_back(count);
            }
            document[JsonKeyPressureTransactionCounts] = std::move(pressureCounts);

            JsonObject poolExhaustionCounts = JsonObject::array();
            for (const std::uint64_t count : sample.poolExhaustionCounts)
            {
                poolExhaustionCounts.push_back(count);
            }
            document[JsonKeyPoolExhaustionCounts] = std::move(poolExhaustionCounts);

            JsonObject memoryPools = JsonObject::array();
            for (const psnr::runtime::NrServerMemoryPoolSnapshot& pool : sample.memoryPools)
            {
                JsonObject poolDocument = JsonObject::object();
                poolDocument[JsonKeyCapacity] = pool.capacity;
                poolDocument[JsonKeyInUse] = pool.inUse;
                poolDocument[JsonKeyAvailable] = pool.available;
                poolDocument[JsonKeyHighWatermark] = pool.highWatermark;
                memoryPools.push_back(std::move(poolDocument));
            }
            document[JsonKeyMemoryPools] = std::move(memoryPools);

            JsonObject diagnostics = JsonObject::object();
            diagnostics[JsonKeyEnabled] = sample.diagnostics.enabled;
            diagnostics[JsonKeySinkFailed] = sample.diagnostics.sinkFailed;
            diagnostics[JsonKeyAttempted] = sample.diagnostics.attempted;
            diagnostics[JsonKeyEnqueued] = sample.diagnostics.enqueued;
            diagnostics[JsonKeyDroppedQueueFull] = sample.diagnostics.droppedQueueFull;
            diagnostics[JsonKeyDroppedSinkUnavailable] = sample.diagnostics.droppedSinkUnavailable;
            diagnostics[JsonKeyConsumed] = sample.diagnostics.consumed;
            diagnostics[JsonKeyDiscardedAfterSinkFailure] = sample.diagnostics.discardedAfterSinkFailure;
            document[JsonKeyDiagnostics] = std::move(diagnostics);

            return document;
        }

        [[nodiscard]] bool DecodeRuntimeSample(const JsonObject& document, BenchmarkRuntimeSampleV1* const outSample)
        {
            if (!document.is_object() || outSample == nullptr)
            {
                return false;
            }

            const JsonObject* lifecycleState = FindProperty(document, JsonKeyLifecycleState);
            const JsonObject* pressureCounts = FindProperty(document, JsonKeyPressureTransactionCounts);
            const JsonObject* poolExhaustionCounts = FindProperty(document, JsonKeyPoolExhaustionCounts);
            const JsonObject* memoryPools = FindProperty(document, JsonKeyMemoryPools);
            const JsonObject* diagnostics = FindProperty(document, JsonKeyDiagnostics);
            if (lifecycleState == nullptr || !lifecycleState->is_string() || pressureCounts == nullptr ||
                !pressureCounts->is_array() ||
                pressureCounts->size() != psnr::runtime::NrPressureTransactionOutcomeCount ||
                poolExhaustionCounts == nullptr || !poolExhaustionCounts->is_array() ||
                poolExhaustionCounts->size() != psnr::runtime::NrServerMemoryPoolRoleCount || memoryPools == nullptr ||
                !memoryPools->is_array() || memoryPools->size() != psnr::runtime::NrServerMemoryPoolRoleCount ||
                diagnostics == nullptr || !diagnostics->is_object())
            {
                return false;
            }

            BenchmarkRuntimeSampleV1 sample;
            if (!ParseLifecycleState(lifecycleState->get<std::string>(), &sample.lifecycleState) ||
                !ReadUnsigned(document, JsonKeyRegisteredSessionCount, &sample.registeredSessionCount) ||
                !ReadUnsigned(document, JsonKeyClosingSessionCount, &sample.closingSessionCount) ||
                !ReadUnsigned(document, JsonKeyPendingRecvIoCount, &sample.pendingRecvIoCount) ||
                !ReadUnsigned(document, JsonKeyPendingSendIoCount, &sample.pendingSendIoCount) ||
                !ReadUnsigned(document, JsonKeyToWorldEventDepth, &sample.toWorldEventDepth) ||
                !ReadUnsigned(document, JsonKeyToWorldEventHighWatermark, &sample.toWorldEventHighWatermark) ||
                !ReadUnsigned(document, JsonKeyTotalPressureTransactions, &sample.totalPressureTransactions))
            {
                return false;
            }

            for (std::size_t index = 0; index < sample.pressureTransactionCounts.size(); ++index)
            {
                const JsonObject& value = (*pressureCounts)[index];
                if (!value.is_number_unsigned())
                {
                    return false;
                }
                sample.pressureTransactionCounts[index] = value.get<std::uint64_t>();
            }

            for (std::size_t index = 0; index < sample.poolExhaustionCounts.size(); ++index)
            {
                const JsonObject& exhaustionValue = (*poolExhaustionCounts)[index];
                const JsonObject& poolDocument = (*memoryPools)[index];
                if (!exhaustionValue.is_number_unsigned() || !poolDocument.is_object())
                {
                    return false;
                }

                sample.poolExhaustionCounts[index] = exhaustionValue.get<std::uint64_t>();
                psnr::runtime::NrServerMemoryPoolSnapshot& pool = sample.memoryPools[index];
                if (!ReadUnsigned(poolDocument, JsonKeyCapacity, &pool.capacity) ||
                    !ReadUnsigned(poolDocument, JsonKeyInUse, &pool.inUse) ||
                    !ReadUnsigned(poolDocument, JsonKeyAvailable, &pool.available) ||
                    !ReadUnsigned(poolDocument, JsonKeyHighWatermark, &pool.highWatermark))
                {
                    return false;
                }
            }

            if (!ReadBoolean(*diagnostics, JsonKeyEnabled, &sample.diagnostics.enabled) ||
                !ReadBoolean(*diagnostics, JsonKeySinkFailed, &sample.diagnostics.sinkFailed) ||
                !ReadUnsigned(*diagnostics, JsonKeyAttempted, &sample.diagnostics.attempted) ||
                !ReadUnsigned(*diagnostics, JsonKeyEnqueued, &sample.diagnostics.enqueued) ||
                !ReadUnsigned(*diagnostics, JsonKeyDroppedQueueFull, &sample.diagnostics.droppedQueueFull) ||
                !ReadUnsigned(*diagnostics, JsonKeyDroppedSinkUnavailable,
                              &sample.diagnostics.droppedSinkUnavailable) ||
                !ReadUnsigned(*diagnostics, JsonKeyConsumed, &sample.diagnostics.consumed) ||
                !ReadUnsigned(*diagnostics, JsonKeyDiscardedAfterSinkFailure,
                              &sample.diagnostics.discardedAfterSinkFailure))
            {
                return false;
            }

            *outSample = std::move(sample);
            return true;
        }
    } // namespace

    BenchmarkIpcEventEncodeResult BenchmarkIpcEventV1Codec::Encode(const BenchmarkIpcEventV1& event)
    {
        const std::string validationError = ValidateEnvelope(event);
        if (!validationError.empty())
        {
            return EncodeFailure(validationError);
        }

        const std::string_view type = TypeToken(event.type);
        if (type.empty())
        {
            return EncodeFailure("event type is unsupported");
        }

        JsonObject document = JsonObject::object();
        document[JsonKeySchema] = std::string(IpcSchemaName);
        document[JsonKeyVersion] = IpcSchemaVersion;
        document[JsonKeyType] = std::string(type);
        document[JsonKeyRunId] = event.runId;
        document[JsonKeySequence] = event.sequence;
        if (event.type == BenchmarkIpcEventType::RuntimeSample)
        {
            document[JsonKeyRuntimeSample] = EncodeRuntimeSample(event.runtimeSample);
        }
        else if (event.type == BenchmarkIpcEventType::Error)
        {
            document[JsonKeyErrorMessage] = event.errorMessage;
        }

        BenchmarkIpcEventEncodeResult result;
        result.json = document.dump();
        return result;
    }

    BenchmarkIpcEventDecodeResult BenchmarkIpcEventV1Codec::Decode(const std::string_view jsonText)
    {
        if (jsonText.empty())
        {
            return DecodeFailure("control event JSON is empty");
        }

        JsonObject document = JsonObject::parse(jsonText, nullptr, false);
        if (document.is_discarded())
        {
            return DecodeFailure("control event JSON is malformed");
        }
        if (!document.is_object())
        {
            return DecodeFailure("control event root must be an object");
        }

        const JsonObject* schema = FindProperty(document, JsonKeySchema);
        const JsonObject* version = FindProperty(document, JsonKeyVersion);
        const JsonObject* type = FindProperty(document, JsonKeyType);
        const JsonObject* runId = FindProperty(document, JsonKeyRunId);
        const JsonObject* sequence = FindProperty(document, JsonKeySequence);
        if (schema == nullptr || version == nullptr || type == nullptr || runId == nullptr || sequence == nullptr)
        {
            return DecodeFailure("control event is missing a required field");
        }
        if (!schema->is_string() || schema->get<std::string>() != IpcSchemaName)
        {
            return DecodeFailure("control event schema is unsupported");
        }
        if (!version->is_number_unsigned() || version->get<std::uint64_t>() != IpcSchemaVersion)
        {
            return DecodeFailure("control event version is unsupported");
        }
        if (!type->is_string() || !runId->is_string() || !sequence->is_number_unsigned())
        {
            return DecodeFailure("control event field type is invalid");
        }

        BenchmarkIpcEventV1 event;
        if (!ParseType(type->get<std::string>(), &event.type))
        {
            return DecodeFailure("control event type is unsupported");
        }
        event.runId = runId->get<std::string>();
        event.sequence = sequence->get<std::uint64_t>();

        if (event.type == BenchmarkIpcEventType::RuntimeSample)
        {
            const JsonObject* runtimeSample = FindProperty(document, JsonKeyRuntimeSample);
            if (runtimeSample == nullptr || !DecodeRuntimeSample(*runtimeSample, &event.runtimeSample))
            {
                return DecodeFailure("runtimeSample event payload is invalid");
            }
        }
        else if (event.type == BenchmarkIpcEventType::Error)
        {
            const JsonObject* errorMessage = FindProperty(document, JsonKeyErrorMessage);
            if (errorMessage == nullptr || !errorMessage->is_string())
            {
                return DecodeFailure("error event payload is invalid");
            }
            event.errorMessage = errorMessage->get<std::string>();
        }

        const std::string validationError = ValidateEnvelope(event);
        if (!validationError.empty())
        {
            return DecodeFailure(validationError);
        }

        BenchmarkIpcEventDecodeResult result;
        result.event = std::move(event);
        return result;
    }
} // namespace psnr::benchmark
