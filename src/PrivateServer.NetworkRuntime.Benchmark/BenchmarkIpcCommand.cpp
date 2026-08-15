#include "BenchmarkIpcCommand.h"

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

        constexpr std::string_view JsonKeySchema = "schema";
        constexpr std::string_view JsonKeyVersion = "version";
        constexpr std::string_view JsonKeyType = "type";
        constexpr std::string_view JsonKeyRunId = "runId";
        constexpr std::string_view JsonKeySequence = "sequence";

        constexpr std::string_view CaptureSnapshotType = "captureSnapshot";
        constexpr std::string_view StopType = "stop";

        using JsonObject = nlohmann::ordered_json;

        [[nodiscard]] BenchmarkIpcCommandEncodeResult EncodeFailure(std::string error)
        {
            BenchmarkIpcCommandEncodeResult result;
            result.error = std::move(error);
            return result;
        }

        [[nodiscard]] BenchmarkIpcCommandDecodeResult DecodeFailure(std::string error)
        {
            BenchmarkIpcCommandDecodeResult result;
            result.error = std::move(error);
            return result;
        }

        [[nodiscard]] std::string ValidateEnvelope(const BenchmarkIpcCommandV1& command)
        {
            if (command.runId.empty() || command.runId.size() > MaximumRunIdBytes)
            {
                return "runId must contain between 1 and 128 bytes";
            }
            if (command.sequence == 0)
            {
                return "sequence must be greater than zero";
            }
            return {};
        }

        [[nodiscard]] std::string_view TypeToken(const BenchmarkIpcCommandType type) noexcept
        {
            switch (type)
            {
            case BenchmarkIpcCommandType::CaptureSnapshot:
                return CaptureSnapshotType;
            case BenchmarkIpcCommandType::Stop:
                return StopType;
            }

            return {};
        }

        [[nodiscard]] bool ParseType(const std::string_view token, BenchmarkIpcCommandType* outType) noexcept
        {
            if (outType == nullptr)
            {
                return false;
            }

            if (token == CaptureSnapshotType)
            {
                *outType = BenchmarkIpcCommandType::CaptureSnapshot;
                return true;
            }
            if (token == StopType)
            {
                *outType = BenchmarkIpcCommandType::Stop;
                return true;
            }
            return false;
        }

        [[nodiscard]] const JsonObject* FindProperty(const JsonObject& document, const std::string_view key)
        {
            const JsonObject::const_iterator iterator = document.find(key);
            return iterator == document.end() ? nullptr : &(*iterator);
        }
    } // namespace

    BenchmarkIpcCommandEncodeResult BenchmarkIpcCommandV1Codec::Encode(const BenchmarkIpcCommandV1& command)
    {
        const std::string validationError = ValidateEnvelope(command);
        if (!validationError.empty())
        {
            return EncodeFailure(validationError);
        }

        const std::string_view type = TypeToken(command.type);
        if (type.empty())
        {
            return EncodeFailure("command type is unsupported");
        }

        JsonObject document = JsonObject::object();
        document[JsonKeySchema] = std::string(IpcSchemaName);
        document[JsonKeyVersion] = IpcSchemaVersion;
        document[JsonKeyType] = std::string(type);
        document[JsonKeyRunId] = command.runId;
        document[JsonKeySequence] = command.sequence;

        BenchmarkIpcCommandEncodeResult result;
        result.json = document.dump();
        return result;
    }

    BenchmarkIpcCommandDecodeResult BenchmarkIpcCommandV1Codec::Decode(const std::string_view jsonText)
    {
        if (jsonText.empty())
        {
            return DecodeFailure("control command JSON is empty");
        }

        JsonObject document = JsonObject::parse(jsonText, nullptr, false);
        if (document.is_discarded())
        {
            return DecodeFailure("control command JSON is malformed");
        }
        if (!document.is_object())
        {
            return DecodeFailure("control command root must be an object");
        }

        const JsonObject* schema = FindProperty(document, JsonKeySchema);
        const JsonObject* version = FindProperty(document, JsonKeyVersion);
        const JsonObject* type = FindProperty(document, JsonKeyType);
        const JsonObject* runId = FindProperty(document, JsonKeyRunId);
        const JsonObject* sequence = FindProperty(document, JsonKeySequence);
        if (schema == nullptr || version == nullptr || type == nullptr || runId == nullptr || sequence == nullptr)
        {
            return DecodeFailure("control command is missing a required field");
        }
        if (!schema->is_string() || schema->get<std::string>() != IpcSchemaName)
        {
            return DecodeFailure("control command schema is unsupported");
        }
        if (!version->is_number_unsigned() || version->get<std::uint64_t>() != IpcSchemaVersion)
        {
            return DecodeFailure("control command version is unsupported");
        }
        if (!type->is_string() || !runId->is_string() || !sequence->is_number_unsigned())
        {
            return DecodeFailure("control command field type is invalid");
        }

        BenchmarkIpcCommandV1 command;
        if (!ParseType(type->get<std::string>(), &command.type))
        {
            return DecodeFailure("control command type is unsupported");
        }
        command.runId = runId->get<std::string>();
        command.sequence = sequence->get<std::uint64_t>();

        const std::string validationError = ValidateEnvelope(command);
        if (!validationError.empty())
        {
            return DecodeFailure(validationError);
        }

        BenchmarkIpcCommandDecodeResult result;
        result.command = std::move(command);
        return result;
    }
} // namespace psnr::benchmark
