#include "pch.h"

#include "NrDebugDiagnosticSink.h"

#include "NrTypeTraits.h"
#include "NrErrorCode.h"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstdio>
#include <iterator>
#include <new>
#include <system_error>
#include <type_traits>
#include <utility>

namespace psnr::runtime::internal
{
    namespace
    {
        // !TODO: Text Writer or file IO 전용 유틸 분리하기
        class NrFixedTextWriter final
        {
        public:
            explicit NrFixedTextWriter(NrDebugDiagnosticLine& output) noexcept
                : output_(output)
            {
                output_.length = 0;
            }

            [[nodiscard]] bool Append(const std::string_view value) noexcept
            {
                if (value.size() > output_.bytes.size() - output_.length)
                {
                    return false;
                }

                std::copy(value.begin(), value.end(), output_.bytes.begin() + output_.length);
                output_.length += value.size();
                return true;
            }

            // 부호 없는 정수를 ASCII 10진수 문자열로 변환한 뒤, 버퍼에 추가
            template <typename TValue> [[nodiscard]] bool AppendUnsigned(const TValue value) noexcept
            {
                static_assert(
                    psnr::core::NrConceptUnsigned<TValue>); // TValue가 unsigned type인지 컴파일 시점 검사 필요

                char buffer[32]{}; // 임시 변환 버퍼

                // to_chars 는 null terminator가 없음.
                // result.ptr이 기록이 끝난 다음 위치를 가리킴. result.ptr - buffer = 실제 변환된 바이트 길이
                const std::to_chars_result result = std::to_chars(std::begin(buffer), std::end(buffer), value);
                return (result.ec == std::errc{}) &&
                       Append(std::string_view(buffer, static_cast<std::size_t>(result.ptr - buffer)));
            }

        private:
            NrDebugDiagnosticLine& output_;
        };

        [[nodiscard]] constexpr bool HasFlag(const NrDiagnosticContextFlags contextFlags,
                                             const NrDiagnosticContextFlags targetFlag) noexcept
        {
            // enum class 는 정수로 자동 변환되지 않음
            // AND 연산을 위한 정수 타입 변환
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

        [[nodiscard]] constexpr std::string_view ErrorToken(const psnr::core::NrErrorCode value) noexcept
        {
            using psnr::core::NrErrorCode;

            switch (value)
            {
            case NrErrorCode::Success:
                return "success";
            case NrErrorCode::InvalidArgument:
                return "invalid_argument";
            case NrErrorCode::InvalidState:
                return "invalid_state";
            case NrErrorCode::OutOfMemory:
                return "out_of_memory";
            case NrErrorCode::PoolExhausted:
                return "pool_exhausted";
            case NrErrorCode::CapacityExceeded:
                return "capacity_exceeded";
            case NrErrorCode::QueueFull:
                return "queue_full";
            case NrErrorCode::QueueEmpty:
                return "queue_empty";
            case NrErrorCode::DispatchRuleNotFound:
                return "dispatch_rule_not_found";
            case NrErrorCode::IoFailed:
                return "io_failed";
            case NrErrorCode::OperationCanceled:
                return "operation_canceled";
            case NrErrorCode::ProtocolError:
                return "protocol_error";
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

        class NrDebugStderrDiagnosticSink final : public INrDiagnosticSink
        {
        public:
            [[nodiscard]] psnr::core::NrStatus Begin(const NrDiagnosticRunMetadata& metadata) noexcept override
            {
                return metadata.mode == NrDiagnosticsMode::Debug
                           ? psnr::core::NrStatus::Success()
                           : psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::InvalidArgument);
            }

            [[nodiscard]] psnr::core::NrStatus Consume(const NrDiagnosticRecord& record) noexcept override
            {
                NrDebugDiagnosticLine line;
                const psnr::core::NrStatus formatStatus = FormatDebugDiagnosticRecord(record, line);
                if (formatStatus.Failed())
                {
                    return formatStatus;
                }

                const std::string_view view = line.View();

                // diagnostic line 을 stderr에 기록
                // view.size 만큼만 출력함(1바이트 단위로)
                if (std::fwrite(view.data(), 1, view.size(), stderr) != view.size())
                {
                    return psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::IoFailed,
                                                         static_cast<psnr::core::NrNativeErrorCode>(errno));
                }

                return psnr::core::NrStatus::Success();
            }

            [[nodiscard]] psnr::core::NrStatus Finish(const NrDiagnosticSummary&) noexcept override
            {
                if (std::fflush(stderr) != 0) // 스트림 내부 버퍼에 남은 데이터 출력을 위한 fflush
                {
                    return psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::IoFailed,
                                                         static_cast<psnr::core::NrNativeErrorCode>(errno));
                }

                return psnr::core::NrStatus::Success();
            }
        };
    } // namespace

    psnr::core::NrStatus FormatDebugDiagnosticRecord(const NrDiagnosticRecord& record,
                                                     NrDebugDiagnosticLine& output) noexcept
    {
        NrFixedTextWriter writer(output);
        if (!writer.Append("nrdiag seq=") || !writer.AppendUnsigned(record.drainSequence) ||
            !writer.Append(" ts_ns=") || !writer.AppendUnsigned(record.producerTimestamp) ||
            !writer.Append(" severity=") || !writer.Append(SeverityToken(record.severity)) ||
            !writer.Append(" kind=") || !writer.Append(EventKindToken(record.eventKind)) ||
            !writer.Append(" component=") || !writer.Append(ComponentToken(record.component)) ||
            !writer.Append(" operation=") || !writer.Append(OperationToken(record.operation)) ||
            !writer.Append(" error=") || !writer.Append(ErrorToken(record.errorCode)) || !writer.Append(" native=") ||
            !writer.AppendUnsigned(record.nativeErrorCode))
        {
            output.length = 0;
            return psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::CapacityExceeded);
        }

        if (HasFlag(record.contextFlags, NrDiagnosticContextFlags::HasSessionKey) &&
            (!writer.Append(" session=") || !writer.AppendUnsigned(record.sessionKey)))
        {
            output.length = 0;
            return psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::CapacityExceeded);
        }

        if (HasFlag(record.contextFlags, NrDiagnosticContextFlags::HasIoOperation) &&
            (!writer.Append(" io=") || !writer.Append(IoOperationToken(record.ioOperation))))
        {
            output.length = 0;
            return psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::CapacityExceeded);
        }

        if (HasFlag(record.contextFlags, NrDiagnosticContextFlags::HasCloseReason) &&
            (!writer.Append(" close_reason=") || !writer.Append(CloseReasonToken(record.closeReason))))
        {
            output.length = 0;
            return psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::CapacityExceeded);
        }

        if (!writer.Append("\n"))
        {
            output.length = 0;
            return psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::CapacityExceeded);
        }

        return psnr::core::NrStatus::Success();
    }

    psnr::core::NrResult<std::unique_ptr<INrDiagnosticSink>> CreateDebugDiagnosticSink() noexcept
    {
        std::unique_ptr<INrDiagnosticSink> sink(new (std::nothrow) NrDebugStderrDiagnosticSink());
        if (sink == nullptr)
        {
            return psnr::core::NrResult<std::unique_ptr<INrDiagnosticSink>>::Failure(
                psnr::core::NrErrorCode::OutOfMemory);
        }

        return psnr::core::NrResult<std::unique_ptr<INrDiagnosticSink>>(std::move(sink));
    }
} // namespace psnr::runtime::internal
