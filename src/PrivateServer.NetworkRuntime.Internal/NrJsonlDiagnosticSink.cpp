#include "pch.h"

#include "NrJsonlDiagnosticSink.h"

#include "NrErrorCode.h"

#include <nlohmann/json.hpp> // header-only

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace psnr::runtime::internal
{
    namespace
    {
        constexpr std::string_view SchemaName = "psnr.network_runtime.diagnostics";
        constexpr std::string_view UnknownEnumString = "unknown";
        constexpr std::uint64_t SchemaVersion = 1;

        constexpr std::string_view JsonKeySchema = "schema";
        constexpr std::string_view JsonKeyVersion = "version";
        constexpr std::string_view JsonKeyType = "type";
        constexpr std::string_view JsonKeyClock = "clock";
        constexpr std::string_view JsonKeyMode = "mode";
        constexpr std::string_view JsonKeyTimestampNs = "tsNs";
        constexpr std::string_view JsonKeySequence = "sequence";
        constexpr std::string_view JsonKeySeverity = "severity";
        constexpr std::string_view JsonKeySeverityValue = "severityValue";
        constexpr std::string_view JsonKeyKind = "kind";
        constexpr std::string_view JsonKeyKindValue = "kindValue";
        constexpr std::string_view JsonKeyComponent = "component";
        constexpr std::string_view JsonKeyComponentValue = "componentValue";
        constexpr std::string_view JsonKeyOperation = "operation";
        constexpr std::string_view JsonKeyOperationValue = "operationValue";
        constexpr std::string_view JsonKeyErrorCode = "errorCode";
        constexpr std::string_view JsonKeyNativeErrorCode = "nativeErrorCode";
        constexpr std::string_view JsonKeySessionKey = "sessionKey";
        constexpr std::string_view JsonKeyIoOperation = "ioOperation";
        constexpr std::string_view JsonKeyIoOperationValue = "ioOperationValue";
        constexpr std::string_view JsonKeyCloseReason = "closeReason";
        constexpr std::string_view JsonKeyCloseReasonValue = "closeReasonValue";
        constexpr std::string_view JsonKeyAttempted = "attempted";
        constexpr std::string_view JsonKeyEnqueued = "enqueued";
        constexpr std::string_view JsonKeyConsumed = "consumed";
        constexpr std::string_view JsonKeyDroppedQueueFull = "droppedQueueFull";
        constexpr std::string_view JsonKeyDroppedSinkUnavailable = "droppedSinkUnavailable";
        constexpr std::string_view JsonKeyDiscardedAfterSinkFailure = "discardedAfterSinkFailure";
        constexpr std::string_view JsonKeyEventFlushSucceeded = "eventFlushSucceeded";

        using NrJsonObject = nlohmann::ordered_json; // 데이터를 삽입한 순서 유지를 위해

        struct NrJsonlDiagnosticLine final
        {
            static constexpr std::size_t Capacity = 1024;

            [[nodiscard]] std::string_view View() const noexcept
            {
                return std::string_view(bytes.data(), length);
            }

            std::array<char, Capacity> bytes{};
            std::size_t length = 0;
        };

        [[nodiscard]] constexpr bool HasFlag(const NrDiagnosticContextFlags contextFlags,
                                             const NrDiagnosticContextFlags targetFlag) noexcept
        {
            using NrFlagsValue = std::underlying_type_t<NrDiagnosticContextFlags>;
            return (static_cast<NrFlagsValue>(contextFlags) & static_cast<NrFlagsValue>(targetFlag)) != 0;
        }

        [[nodiscard]] constexpr std::string_view SeverityToken(const NrDiagnosticSeverity value) noexcept
        {
            switch (value)
            {
            case NrDiagnosticSeverity::Info:
                return "info";
            case NrDiagnosticSeverity::Warning:
                return "warning";
            case NrDiagnosticSeverity::Error:
                return "error";
            default:
                return "unknown";
            }
        }

        [[nodiscard]] constexpr std::string_view EventKindToken(const NrDiagnosticEventKind value) noexcept
        {
            switch (value)
            {
            case NrDiagnosticEventKind::Transition:
                return "transition";
            case NrDiagnosticEventKind::Failure:
                return "failure";
            case NrDiagnosticEventKind::Anomaly:
                return "anomaly";
            default:
                return "unknown";
            }
        }

        [[nodiscard]] constexpr std::string_view ComponentToken(const NrDiagnosticComponent value) noexcept
        {
            switch (value)
            {
            case NrDiagnosticComponent::ServerLifecycle:
                return "server_lifecycle";
            case NrDiagnosticComponent::IoPipeline:
                return "io_pipeline";
            case NrDiagnosticComponent::Session:
                return "session";
            case NrDiagnosticComponent::ActorScheduler:
                return "actor_scheduler";
            case NrDiagnosticComponent::ToWorldHandoff:
                return "to_world_handoff";
            case NrDiagnosticComponent::MemoryPool:
                return "memory_pool";
            default:
                return "unknown";
            }
        }

        [[nodiscard]] constexpr std::string_view OperationToken(const NrDiagnosticOperation value) noexcept
        {
            switch (value)
            {
            case NrDiagnosticOperation::Configure:
                return "configure";
            case NrDiagnosticOperation::Start:
                return "start";
            case NrDiagnosticOperation::RequestStop:
                return "request_stop";
            case NrDiagnosticOperation::Shutdown:
                return "shutdown";
            case NrDiagnosticOperation::Accept:
                return "accept";
            case NrDiagnosticOperation::Receive:
                return "receive";
            case NrDiagnosticOperation::Send:
                return "send";
            case NrDiagnosticOperation::Close:
                return "close";
            case NrDiagnosticOperation::Admission:
                return "admission";
            case NrDiagnosticOperation::Acquire:
                return "acquire";
            case NrDiagnosticOperation::Post:
                return "post";
            case NrDiagnosticOperation::Complete:
                return "complete";
            default:
                return "unknown";
            }
        }

        [[nodiscard]] constexpr std::string_view IoOperationToken(const NrDiagnosticIoOperation value) noexcept
        {
            switch (value)
            {
            case NrDiagnosticIoOperation::Accept:
                return "accept";
            case NrDiagnosticIoOperation::Receive:
                return "recv";
            case NrDiagnosticIoOperation::Send:
                return "send";
            default:
                return "unknown";
            }
        }

        [[nodiscard]] constexpr std::string_view CloseReasonToken(const NrSessionEndReason value) noexcept
        {
            switch (value)
            {
            case NrSessionEndReason::ApplicationRequested:
                return "application_requested";
            case NrSessionEndReason::ApplicationPolicy:
                return "application_policy";
            case NrSessionEndReason::ProtocolError:
                return "protocol_error";
            case NrSessionEndReason::RemoteClosed:
                return "remote_closed";
            case NrSessionEndReason::ReceivePressure:
                return "receive_pressure";
            case NrSessionEndReason::SendPressure:
                return "send_pressure";
            case NrSessionEndReason::TransportError:
                return "transport_error";
            case NrSessionEndReason::ServerStopping:
                return "server_stopping";
            case NrSessionEndReason::None:
                return "none";
            default:
                return "unknown";
            }
        }

        template <typename TEnum>
        void AddUnknownRawValue(NrJsonObject& document, const std::string_view token,
                                const std::string_view propertyName, const TEnum value)
        {
            static_assert(std::is_enum_v<TEnum>);
            if (token != UnknownEnumString)
            {
                return;
            }

            // add unknown raw enum value
            // {
            //      ...
            //      "${propertyName}" : ${value},
            //      ...
            // }
            using Underlying = std::underlying_type_t<TEnum>;            // enum 의 실제 저장 타입 구하기
            using UnsignedUnderlying = std::make_unsigned_t<Underlying>; // 같은 크기의 unsinged 타입 구하기
            const std::uint64_t raw = static_cast<std::uint64_t>(static_cast<UnsignedUnderlying>(value));
            document[propertyName] = raw;
        }

        [[nodiscard]] psnr::core::NrStatus CapacityExceeded() noexcept
        {
            return psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::CapacityExceeded);
        }

        [[nodiscard]] psnr::core::NrStatus SerializationFailed() noexcept
        {
            return psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::InvalidState);
        }

        template <typename TBuilder>
        [[nodiscard]] psnr::core::NrStatus FormatJsonLine(const TBuilder& builder,
                                                          NrJsonlDiagnosticLine& output) noexcept
        {
            output.length = 0;
            try
            {
                NrJsonObject document = NrJsonObject::object();
                builder(document);

                const std::string serialized = document.dump(); // one-line serialization
                if (serialized.size() >= output.bytes.size())
                {
                    return CapacityExceeded();
                }

                std::copy(serialized.begin(), serialized.end(), output.bytes.begin());
                output.bytes[serialized.size()] = '\n';
                output.length = serialized.size() + 1;

                return psnr::core::NrStatus::Success();
            }
            catch (const std::bad_alloc&)
            {
                return psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::OutOfMemory);
            }
            catch (const std::length_error&)
            {
                return CapacityExceeded();
            }
            catch (const nlohmann::json::exception&)
            {
                return SerializationFailed();
            }
            catch (...)
            {
                return SerializationFailed();
            }
        }

        [[nodiscard]] psnr::core::NrStatus FormatRunMetadata(const NrDiagnosticRunMetadata& metadata,
                                                             NrJsonlDiagnosticLine& output) noexcept
        {
            if (metadata.mode != NrDiagnosticsMode::Benchmark)
            {
                return psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::InvalidArgument);
            }

            return FormatJsonLine(
                [](NrJsonObject& document)
                {
                    document[JsonKeySchema] = std::string(SchemaName);
                    document[JsonKeyVersion] = SchemaVersion;
                    document[JsonKeyType] = "run";
                    document[JsonKeyClock] = "steady_ns";
                    document[JsonKeyMode] = "benchmark";
                },
                output);
        }

        [[nodiscard]] psnr::core::NrStatus FormatEvent(const NrDiagnosticRecord& record,
                                                       NrJsonlDiagnosticLine& output) noexcept
        {
            const std::string_view severity = SeverityToken(record.severity);
            const std::string_view kind = EventKindToken(record.eventKind);
            const std::string_view component = ComponentToken(record.component);
            const std::string_view operation = OperationToken(record.operation);

            return FormatJsonLine(
                [&record, severity, kind, component, operation](NrJsonObject& document)
                {
                    document[JsonKeySchema] = std::string(SchemaName);
                    document[JsonKeyVersion] = SchemaVersion;
                    document[JsonKeyType] = "event";
                    document[JsonKeyTimestampNs] = record.producerTimestamp;
                    document[JsonKeySequence] = record.drainSequence;
                    document[JsonKeySeverity] = std::string(severity);
                    AddUnknownRawValue(document, severity, JsonKeySeverityValue, record.severity);
                    document[JsonKeyKind] = std::string(kind);
                    AddUnknownRawValue(document, kind, JsonKeyKindValue, record.eventKind);
                    document[JsonKeyComponent] = std::string(component);
                    AddUnknownRawValue(document, component, JsonKeyComponentValue, record.component);
                    document[JsonKeyOperation] = std::string(operation);
                    AddUnknownRawValue(document, operation, JsonKeyOperationValue, record.operation);
                    document[JsonKeyErrorCode] = static_cast<std::uint32_t>(record.errorCode);
                    document[JsonKeyNativeErrorCode] = record.nativeErrorCode;

                    if (HasFlag(record.contextFlags, NrDiagnosticContextFlags::HasSessionKey))
                    {
                        document[JsonKeySessionKey] = record.sessionKey;
                    }

                    if (HasFlag(record.contextFlags, NrDiagnosticContextFlags::HasIoOperation))
                    {
                        const std::string_view ioOperation = IoOperationToken(record.ioOperation);
                        document[JsonKeyIoOperation] = std::string(ioOperation);
                        AddUnknownRawValue(document, ioOperation, JsonKeyIoOperationValue, record.ioOperation);
                    }

                    if (HasFlag(record.contextFlags, NrDiagnosticContextFlags::HasCloseReason))
                    {
                        const std::string_view closeReason = CloseReasonToken(record.closeReason);
                        document[JsonKeyCloseReason] = std::string(closeReason);
                        AddUnknownRawValue(document, closeReason, JsonKeyCloseReasonValue, record.closeReason);
                    }
                },
                output);
        }

        [[nodiscard]] psnr::core::NrStatus FormatSummary(const NrDiagnosticSummary& summary,
                                                         NrJsonlDiagnosticLine& output) noexcept
        {
            return FormatJsonLine(
                [&summary](NrJsonObject& document)
                {
                    document[JsonKeySchema] = std::string(SchemaName);
                    document[JsonKeyVersion] = SchemaVersion;
                    document[JsonKeyType] = "summary";
                    document[JsonKeyAttempted] = summary.attempted;
                    document[JsonKeyEnqueued] = summary.enqueued;
                    document[JsonKeyConsumed] = summary.consumed;
                    document[JsonKeyDroppedQueueFull] = summary.droppedQueueFull;
                    document[JsonKeyDroppedSinkUnavailable] = summary.droppedSinkUnavailable;
                    document[JsonKeyDiscardedAfterSinkFailure] = summary.discardedAfterSinkFailure;
                    document[JsonKeyEventFlushSucceeded] = true;
                },
                output);
        }

        [[nodiscard]] psnr::core::NrResult<std::wstring> ConvertUtf8Path(const std::string_view path) noexcept
        {
            using psnr::core::NrErrorCode;
            using psnr::core::NrResult;

            if (path.empty() || path.find('\0') != std::string_view::npos)
            {
                return NrResult<std::wstring>::Failure(NrErrorCode::InvalidArgument);
            }

            if (path.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            {
                return NrResult<std::wstring>::Failure(NrErrorCode::CapacityExceeded);
            }

            const int sourceLength = static_cast<int>(path.size());
            const int requiredLength =
                MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), sourceLength, nullptr, 0);
            if (requiredLength == 0)
            {
                return NrResult<std::wstring>::Failure(NrErrorCode::InvalidArgument,
                                                       static_cast<psnr::core::NrNativeErrorCode>(GetLastError()));
            }

            try
            {
                std::wstring widePath(static_cast<std::size_t>(requiredLength), L'\0');
                if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), sourceLength, widePath.data(),
                                        requiredLength) != requiredLength)
                {
                    return NrResult<std::wstring>::Failure(NrErrorCode::InvalidArgument,
                                                           static_cast<psnr::core::NrNativeErrorCode>(GetLastError()));
                }

                return NrResult<std::wstring>(std::move(widePath));
            }
            catch (const std::bad_alloc&)
            {
                return NrResult<std::wstring>::Failure(NrErrorCode::OutOfMemory);
            }
            catch (const std::length_error&)
            {
                return NrResult<std::wstring>::Failure(NrErrorCode::CapacityExceeded);
            }
        }

        class NrJsonlFileDiagnosticSink final : public INrDiagnosticSink
        {
        public:
            explicit NrJsonlFileDiagnosticSink(std::string outputPathUtf8) noexcept
                : outputPathUtf8_(std::move(outputPathUtf8))
            {
            }

            ~NrJsonlFileDiagnosticSink() noexcept override
            {
                if (file_ != nullptr)
                {
                    std::fclose(file_);
                }
            }

            [[nodiscard]] psnr::core::NrStatus Begin(const NrDiagnosticRunMetadata& metadata) noexcept override
            {
                if (metadata.mode != NrDiagnosticsMode::Benchmark)
                {
                    return psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::InvalidArgument);
                }

                if (file_ != nullptr)
                {
                    return psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::InvalidState);
                }

                psnr::core::NrResult<std::wstring> widePathResult = ConvertUtf8Path(outputPathUtf8_);
                if (widePathResult.Failed())
                {
                    return widePathResult.Status();
                }

                std::wstring widePath = widePathResult.TakeValue();
                FILE* openedFile = nullptr;
                const errno_t openError = _wfopen_s(&openedFile, widePath.c_str(), L"wb");
                if (openError != 0 || openedFile == nullptr)
                {
                    return psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::IoFailed,
                                                         static_cast<psnr::core::NrNativeErrorCode>(openError));
                }

                file_ = openedFile;
                NrJsonlDiagnosticLine line;
                const psnr::core::NrStatus formatStatus = FormatRunMetadata(metadata, line);
                if (formatStatus.Failed())
                {
                    CloseIgnoringErrors();
                    return formatStatus;
                }

                const psnr::core::NrStatus writeStatus = Write(line.View());
                if (writeStatus.Failed())
                {
                    CloseIgnoringErrors();
                }
                return writeStatus;
            }

            [[nodiscard]] psnr::core::NrStatus Consume(const NrDiagnosticRecord& record) noexcept override
            {
                if (file_ == nullptr)
                {
                    return psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::InvalidState);
                }

                NrJsonlDiagnosticLine line;
                const psnr::core::NrStatus formatStatus = FormatEvent(record, line);
                return formatStatus.Failed() ? formatStatus : Write(line.View());
            }

            [[nodiscard]] psnr::core::NrStatus Finish(const NrDiagnosticSummary& summary) noexcept override
            {
                if (file_ == nullptr)
                {
                    return psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::InvalidState);
                }

                if (std::fflush(file_) != 0)
                {
                    return IoFailure();
                }

                NrJsonlDiagnosticLine line;
                const psnr::core::NrStatus formatStatus = FormatSummary(summary, line);
                if (formatStatus.Failed())
                {
                    return formatStatus;
                }

                const psnr::core::NrStatus writeStatus = Write(line.View());
                if (writeStatus.Failed())
                {
                    return writeStatus;
                }

                if (std::fflush(file_) != 0)
                {
                    return IoFailure();
                }

                FILE* finishedFile = file_;
                file_ = nullptr;
                if (std::fclose(finishedFile) != 0)
                {
                    return IoFailure();
                }

                return psnr::core::NrStatus::Success();
            }

        private:
            [[nodiscard]] psnr::core::NrStatus Write(const std::string_view bytes) noexcept
            {
                if (std::fwrite(bytes.data(), 1, bytes.size(), file_) != bytes.size())
                {
                    return IoFailure();
                }
                return psnr::core::NrStatus::Success();
            }

            [[nodiscard]] static psnr::core::NrStatus IoFailure() noexcept
            {
                return psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::IoFailed,
                                                     static_cast<psnr::core::NrNativeErrorCode>(errno));
            }

            void CloseIgnoringErrors() noexcept
            {
                FILE* failedFile = file_;
                file_ = nullptr;
                if (failedFile != nullptr)
                {
                    std::fclose(failedFile);
                }
            }

            std::string outputPathUtf8_;
            FILE* file_ = nullptr;
        };
    } // namespace

    psnr::core::NrResult<std::unique_ptr<INrDiagnosticSink>> CreateJsonlDiagnosticSink(
        const std::string_view outputPathUtf8) noexcept
    {
        if (outputPathUtf8.empty())
        {
            return psnr::core::NrResult<std::unique_ptr<INrDiagnosticSink>>::Failure(
                psnr::core::NrErrorCode::InvalidArgument);
        }

        std::string ownedPath;
        try
        {
            ownedPath.assign(outputPathUtf8);
        }
        catch (const std::bad_alloc&)
        {
            return psnr::core::NrResult<std::unique_ptr<INrDiagnosticSink>>::Failure(
                psnr::core::NrErrorCode::OutOfMemory);
        }
        catch (const std::length_error&)
        {
            return psnr::core::NrResult<std::unique_ptr<INrDiagnosticSink>>::Failure(
                psnr::core::NrErrorCode::CapacityExceeded);
        }

        std::unique_ptr<INrDiagnosticSink> sink(new (std::nothrow) NrJsonlFileDiagnosticSink(std::move(ownedPath)));
        if (sink == nullptr)
        {
            return psnr::core::NrResult<std::unique_ptr<INrDiagnosticSink>>::Failure(
                psnr::core::NrErrorCode::OutOfMemory);
        }

        return psnr::core::NrResult<std::unique_ptr<INrDiagnosticSink>>(std::move(sink));
    }
} // namespace psnr::runtime::internal
