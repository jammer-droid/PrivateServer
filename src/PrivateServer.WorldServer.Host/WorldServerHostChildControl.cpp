#include "WorldServerHostChildControl.h"

#define NOMINMAX
#include <Windows.h>
#include <nlohmann/json.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <utility>

namespace psnr::world::host
{
    namespace
    {
        constexpr std::string_view IpcSchemaName = "psnr.network_runtime.benchmark.control";
        constexpr std::uint64_t IpcSchemaVersion = 1;
        constexpr std::size_t MaximumRunIdBytes = 128;
        constexpr std::size_t MaximumErrorMessageBytes = 1024;
        constexpr std::size_t MaximumLineBytes = 64 * 1024;
        constexpr std::size_t ReadBufferBytes = 4096;

        constexpr std::string_view JsonKeySchema = "schema";
        constexpr std::string_view JsonKeyVersion = "version";
        constexpr std::string_view JsonKeyType = "type";
        constexpr std::string_view JsonKeyRunId = "runId";
        constexpr std::string_view JsonKeySequence = "sequence";
        constexpr std::string_view JsonKeyErrorMessage = "errorMessage";

        constexpr std::string_view StopType = "stop";
        constexpr std::string_view ReadyType = "ready";
        constexpr std::string_view ErrorType = "error";
        constexpr std::string_view StoppedType = "stopped";

        using JsonObject = nlohmann::ordered_json;
    } // namespace

    WorldServerHostChildControl::WorldServerHostChildControl(std::string runId, void* const commandReadHandle,
                                                             void* const eventWriteHandle) noexcept
        : runId_(std::move(runId))
        , commandReadHandle_(commandReadHandle)
        , eventWriteHandle_(eventWriteHandle)
    {
    }

    WorldServerHostChildControl::~WorldServerHostChildControl() noexcept
    {
        if (IsValidNativeHandle(commandReadHandle_))
        {
            static_cast<void>(CloseHandle(static_cast<HANDLE>(commandReadHandle_)));
        }
        if (IsValidNativeHandle(eventWriteHandle_))
        {
            static_cast<void>(CloseHandle(static_cast<HANDLE>(eventWriteHandle_)));
        }
    }

    WorldResult<std::unique_ptr<WorldServerHostChildControl>, WorldServerHostChildControlFailure>
    WorldServerHostChildControl::Create(std::string runId, const std::uint64_t commandReadHandle,
                                        const std::uint64_t eventWriteHandle)
    {
        if (runId.empty() || runId.size() > MaximumRunIdBytes || commandReadHandle == 0 || eventWriteHandle == 0 ||
            commandReadHandle > std::numeric_limits<std::uintptr_t>::max() ||
            eventWriteHandle > std::numeric_limits<std::uintptr_t>::max())
        {
            return WorldResult<std::unique_ptr<WorldServerHostChildControl>,
                               WorldServerHostChildControlFailure>::Failure(Failure("child control config is invalid"));
        }

        void* const nativeCommandReadHandle = reinterpret_cast<void*>(static_cast<std::uintptr_t>(commandReadHandle));
        void* const nativeEventWriteHandle = reinterpret_cast<void*>(static_cast<std::uintptr_t>(eventWriteHandle));
        if (!IsValidNativeHandle(nativeCommandReadHandle) || !IsValidNativeHandle(nativeEventWriteHandle) ||
            nativeCommandReadHandle == nativeEventWriteHandle || !IsPipeHandle(nativeCommandReadHandle) ||
            !IsPipeHandle(nativeEventWriteHandle))
        {
            return WorldResult<std::unique_ptr<WorldServerHostChildControl>,
                               WorldServerHostChildControlFailure>::Failure(Failure("child control handle is invalid"));
        }

        try
        {
            std::unique_ptr<WorldServerHostChildControl> control{
                new WorldServerHostChildControl(std::move(runId), nativeCommandReadHandle, nativeEventWriteHandle)};
            return WorldResult<std::unique_ptr<WorldServerHostChildControl>, WorldServerHostChildControlFailure>{
                std::move(control)};
        }
        catch (const std::bad_alloc&)
        {
            return WorldResult<std::unique_ptr<WorldServerHostChildControl>,
                               WorldServerHostChildControlFailure>::Failure(Failure("child control allocation failed"));
        }
    }

    WorldResult<WorldServerHostChildControlCommand, WorldServerHostChildControlFailure> WorldServerHostChildControl::
        ReadCommand()
    {
        WorldResult<std::string, WorldServerHostChildControlFailure> lineResult = ReadLine();
        if (lineResult.Failed())
        {
            return WorldResult<WorldServerHostChildControlCommand, WorldServerHostChildControlFailure>::Failure(
                lineResult.Error());
        }

        JsonObject document = JsonObject::parse(lineResult.Value(), nullptr, false);
        if (document.is_discarded() || !document.is_object())
        {
            return WorldResult<WorldServerHostChildControlCommand, WorldServerHostChildControlFailure>::Failure(
                Failure("control command JSON is malformed"));
        }

        const JsonObject::const_iterator schema = document.find(JsonKeySchema);
        const JsonObject::const_iterator version = document.find(JsonKeyVersion);
        const JsonObject::const_iterator type = document.find(JsonKeyType);
        const JsonObject::const_iterator runId = document.find(JsonKeyRunId);
        const JsonObject::const_iterator sequence = document.find(JsonKeySequence);
        if (schema == document.end() || version == document.end() || type == document.end() ||
            runId == document.end() || sequence == document.end())
        {
            return WorldResult<WorldServerHostChildControlCommand, WorldServerHostChildControlFailure>::Failure(
                Failure("control command is missing a required field"));
        }
        if (!schema->is_string() || schema->get<std::string>() != IpcSchemaName || !version->is_number_unsigned() ||
            version->get<std::uint64_t>() != IpcSchemaVersion || !type->is_string() || !runId->is_string() ||
            runId->get<std::string>() != runId_ || !sequence->is_number_unsigned() ||
            sequence->get<std::uint64_t>() == 0)
        {
            return WorldResult<WorldServerHostChildControlCommand, WorldServerHostChildControlFailure>::Failure(
                Failure("control command envelope is invalid"));
        }

        const std::string commandType = type->get<std::string>();
        if (commandType != StopType)
        {
            return WorldResult<WorldServerHostChildControlCommand, WorldServerHostChildControlFailure>::Failure(
                Failure("control command type is unsupported"));
        }

        WorldServerHostChildControlCommand command;
        command.sequence = sequence->get<std::uint64_t>();
        return WorldResult<WorldServerHostChildControlCommand, WorldServerHostChildControlFailure>{command};
    }

    WorldResult<void, WorldServerHostChildControlFailure> WorldServerHostChildControl::WriteReady()
    {
        return WriteEvent(ReadyType, 0);
    }

    WorldResult<void, WorldServerHostChildControlFailure> WorldServerHostChildControl::WriteStopped(
        const std::uint64_t stopSequence)
    {
        if (stopSequence == 0)
        {
            return WorldResult<void, WorldServerHostChildControlFailure>::Failure(
                Failure("stopped event sequence must be greater than zero"));
        }
        return WriteEvent(StoppedType, stopSequence);
    }

    WorldResult<void, WorldServerHostChildControlFailure> WorldServerHostChildControl::WriteError(
        const std::uint64_t sequence, const std::string_view message)
    {
        if (message.empty() || message.size() > MaximumErrorMessageBytes)
        {
            return WorldResult<void, WorldServerHostChildControlFailure>::Failure(
                Failure("error event message must contain between 1 and 1024 bytes"));
        }
        return WriteEvent(ErrorType, sequence, message);
    }

    bool WorldServerHostChildControl::IsValidNativeHandle(void* const handle) noexcept
    {
        return handle != nullptr && handle != INVALID_HANDLE_VALUE;
    }

    bool WorldServerHostChildControl::IsPipeHandle(void* const handle) noexcept
    {
        return IsValidNativeHandle(handle) && GetFileType(static_cast<HANDLE>(handle)) == FILE_TYPE_PIPE;
    }

    WorldServerHostChildControlFailure WorldServerHostChildControl::Failure(std::string message,
                                                                            const std::uint32_t nativeErrorCode)
    {
        WorldServerHostChildControlFailure failure;
        failure.message = std::move(message);
        failure.nativeErrorCode = nativeErrorCode;
        return failure;
    }

    WorldResult<std::string, WorldServerHostChildControlFailure> WorldServerHostChildControl::ReadLine()
    {
        while (true)
        {
            const std::size_t newlineIndex = pendingCommandBytes_.find('\n');
            if (newlineIndex != std::string::npos)
            {
                if (newlineIndex > MaximumLineBytes)
                {
                    return WorldResult<std::string, WorldServerHostChildControlFailure>::Failure(
                        Failure("control command exceeds the maximum line size"));
                }

                std::string line{pendingCommandBytes_.data(), newlineIndex};
                pendingCommandBytes_.erase(0, newlineIndex + 1);
                return WorldResult<std::string, WorldServerHostChildControlFailure>{std::move(line)};
            }
            if (pendingCommandBytes_.size() > MaximumLineBytes)
            {
                return WorldResult<std::string, WorldServerHostChildControlFailure>::Failure(
                    Failure("control command exceeds the maximum line size"));
            }

            std::array<char, ReadBufferBytes> buffer{};
            DWORD readBytes = 0;
            const BOOL readSucceeded = ReadFile(static_cast<HANDLE>(commandReadHandle_), buffer.data(),
                                                static_cast<DWORD>(buffer.size()), &readBytes, nullptr);
            if (readSucceeded == FALSE)
            {
                return WorldResult<std::string, WorldServerHostChildControlFailure>::Failure(
                    Failure("failed to read control command", static_cast<std::uint32_t>(GetLastError())));
            }
            if (readBytes == 0)
            {
                return WorldResult<std::string, WorldServerHostChildControlFailure>::Failure(
                    Failure("control command pipe reached end of stream"));
            }
            pendingCommandBytes_.append(buffer.data(), readBytes);
        }
    }

    WorldResult<void, WorldServerHostChildControlFailure> WorldServerHostChildControl::WriteLine(
        const std::string_view line)
    {
        if (line.size() > MaximumLineBytes || line.find('\n') != std::string_view::npos)
        {
            return WorldResult<void, WorldServerHostChildControlFailure>::Failure(
                Failure("control event line is invalid"));
        }

        std::string framedLine{line};
        framedLine.push_back('\n');
        std::size_t writtenTotal = 0;
        while (writtenTotal < framedLine.size())
        {
            DWORD writtenBytes = 0;
            const BOOL writeSucceeded =
                WriteFile(static_cast<HANDLE>(eventWriteHandle_), framedLine.data() + writtenTotal,
                          static_cast<DWORD>(framedLine.size() - writtenTotal), &writtenBytes, nullptr);
            if (writeSucceeded == FALSE)
            {
                return WorldResult<void, WorldServerHostChildControlFailure>::Failure(
                    Failure("failed to write control event", static_cast<std::uint32_t>(GetLastError())));
            }
            if (writtenBytes == 0)
            {
                return WorldResult<void, WorldServerHostChildControlFailure>::Failure(
                    Failure("control event pipe wrote zero bytes"));
            }
            writtenTotal += writtenBytes;
        }
        return WorldResult<void, WorldServerHostChildControlFailure>::Success();
    }

    WorldResult<void, WorldServerHostChildControlFailure> WorldServerHostChildControl::WriteEvent(
        const std::string_view type, const std::uint64_t sequence, const std::string_view errorMessage)
    {
        JsonObject document = JsonObject::object();
        document[JsonKeySchema] = std::string{IpcSchemaName};
        document[JsonKeyVersion] = IpcSchemaVersion;
        document[JsonKeyType] = std::string{type};
        document[JsonKeyRunId] = runId_;
        document[JsonKeySequence] = sequence;
        if (type == ErrorType)
        {
            document[JsonKeyErrorMessage] = std::string{errorMessage};
        }
        return WriteLine(document.dump());
    }
} // namespace psnr::world::host
