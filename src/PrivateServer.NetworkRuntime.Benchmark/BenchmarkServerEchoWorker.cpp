#include "BenchmarkServerEchoWorker.h"

#include "BenchmarkProtocol.h"

#include <PrivateServer/NetworkRuntime/NrByteView.h>
#include <PrivateServer/NetworkRuntime/NrErrorCode.h>
#include <PrivateServer/NetworkRuntime/NrServer.h>
#include <PrivateServer/NetworkRuntime/NrSessionSendChannel.h>
#include <PrivateServer/NetworkRuntime/NrStatus.h>
#include <PrivateServer/NetworkRuntime/NrToWorldEvent.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <thread>
#include <utility>

namespace psnr::benchmark
{
    namespace
    {
        constexpr std::chrono::milliseconds IdlePollInterval{1};

        [[nodiscard]] std::string StatusError(const char* const operation, const psnr::core::NrStatus status)
        {
            std::string error(operation);
            error.append(" failed with errorCode=");
            error.append(std::to_string(static_cast<int>(status.ErrorCode())));
            error.append(" nativeErrorCode=");
            error.append(std::to_string(status.NativeErrorCode()));
            return error;
        }

        [[nodiscard]] std::uint64_t SteadyNanosecondsNow() noexcept
        {
            const std::int64_t nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                 std::chrono::steady_clock::now().time_since_epoch())
                                                 .count();
            return nanoseconds > 0 ? static_cast<std::uint64_t>(nanoseconds) : 0;
        }
    } // namespace

    BenchmarkServerEchoWorker::~BenchmarkServerEchoWorker() noexcept
    {
        BeginDrainOnly();
        DrainRemainingAndJoin();
    }

    bool BenchmarkServerEchoWorker::Start(psnr::runtime::NrServer* const server, std::string* const outError)
    {
        if (server == nullptr || !server->IsValid() || thread_.joinable())
        {
            if (outError != nullptr)
            {
                *outError = "Echo worker requires a running server and must start only once";
            }
            return false;
        }

        const psnr::core::NrStatus gatewayStatus = server->CreateGateway(&gateway_);
        if (gatewayStatus.Failed())
        {
            if (outError != nullptr)
            {
                *outError = StatusError("NrServer::CreateGateway", gatewayStatus);
            }
            return false;
        }

        server_ = server;
        try
        {
            thread_ = std::thread(&BenchmarkServerEchoWorker::ThreadMain, this);
        }
        catch (const std::exception& exception)
        {
            server_ = nullptr;
            gateway_ = psnr::runtime::NrGateway{};
            if (outError != nullptr)
            {
                *outError = std::string("failed to start Echo worker: ") + exception.what();
            }
            return false;
        }
        catch (...)
        {
            server_ = nullptr;
            gateway_ = psnr::runtime::NrGateway{};
            if (outError != nullptr)
            {
                *outError = "failed to start Echo worker";
            }
            return false;
        }
        return true;
    }

    void BenchmarkServerEchoWorker::BeginDrainOnly() noexcept
    {
        drainOnly_.store(true, std::memory_order_release);
    }

    void BenchmarkServerEchoWorker::DrainRemainingAndJoin() noexcept
    {
        stopAfterDrain_.store(true, std::memory_order_release);
        if (thread_.joinable())
        {
            thread_.join();
        }
    }

    bool BenchmarkServerEchoWorker::TryGetFailure(std::string* const outError) const
    {
        if (!failed_.load(std::memory_order_acquire))
        {
            return false;
        }

        if (outError != nullptr)
        {
            const std::lock_guard<std::mutex> lock(failureMutex_);
            *outError = failure_;
        }
        return true;
    }

    void BenchmarkServerEchoWorker::ThreadMain() noexcept
    {
        try
        {
            while (true)
            {
                psnr::runtime::NrToWorldEvent event;
                const psnr::core::NrStatus popStatus = server_->TryPopToWorldEvent(&event);
                if (popStatus.Succeeded())
                {
                    if (!ProcessEvent(&event))
                    {
                        return;
                    }
                    continue;
                }

                if (popStatus.ErrorCode() != psnr::core::NrErrorCode::QueueEmpty)
                {
                    const std::string error = StatusError("NrServer::TryPopToWorldEvent", popStatus);
                    RecordFailure(error);
                    return;
                }

                if (stopAfterDrain_.load(std::memory_order_acquire))
                {
                    return;
                }
                std::this_thread::sleep_for(IdlePollInterval);
            }
        }
        catch (...)
        {
            RecordFailure("Echo worker failed with an exception");
        }
    }

    bool BenchmarkServerEchoWorker::ProcessEvent(psnr::runtime::NrToWorldEvent* const event)
    {
        if (event == nullptr || !event->IsValid())
        {
            RecordFailure("Echo worker received an invalid ToWorld event");
            return false;
        }

        switch (event->Kind())
        {
        case psnr::runtime::NrToWorldEventKind::SessionAccepted: // session accepted and insert send channel
        {
            psnr::runtime::NrSessionSendChannel sendChannel;
            const psnr::core::NrStatus channelStatus = event->GetSendChannel(&sendChannel);
            if (channelStatus.Failed())
            {
                RecordFailure("Echo worker failed to read SessionAccepted send channel");
                return false;
            }

            const psnr::core::NrSessionKey sessionKey = event->SessionKey();
            const bool inserted = activeSendChannels_.try_emplace(sessionKey, std::move(sendChannel)).second;
            if (!inserted)
            {
                RecordFailure("Echo worker received duplicate SessionAccepted");
                return false;
            }
            return true;
        }
        case psnr::runtime::NrToWorldEventKind::SessionClosed: // session closed and erase send channel
        {
            const psnr::core::NrSessionKey sessionKey = event->SessionKey();
            if (activeSendChannels_.erase(sessionKey) != 1)
            {
                RecordFailure("Echo worker received SessionClosed before SessionAccepted");
                return false;
            }
            return true;
        }
        case psnr::runtime::NrToWorldEventKind::PacketReceived:
            break;

        case psnr::runtime::NrToWorldEventKind::None:

        default:
            RecordFailure("Echo worker received an unsupported ToWorld event kind");
            return false;
        }

        // process PacketReceived phase

        if (drainOnly_.load(std::memory_order_acquire))
        {
            return true;
        }

        const std::uint64_t serverReceivedTimestampNanoseconds = SteadyNanosecondsNow();
        if (serverReceivedTimestampNanoseconds == 0)
        {
            RecordFailure("Echo worker failed to capture the request timestamp");
            return false;
        }

        psnr::core::NrPacketType packetType;
        psnr::runtime::NrByteView payload;
        const psnr::core::NrStatus packetTypeStatus = event->GetPacketType(&packetType);
        const psnr::core::NrStatus payloadStatus = event->GetPayload(&payload);
        if (packetTypeStatus.Failed() || payloadStatus.Failed())
        {
            RecordFailure("Echo worker failed to read PacketReceived event fields");
            return false;
        }

        const psnr::core::NrSessionKey sessionKey = event->SessionKey();
        const std::unordered_map<psnr::core::NrSessionKey, psnr::runtime::NrSessionSendChannel>::const_iterator
            channelIterator = activeSendChannels_.find(sessionKey);
        if (channelIterator == activeSendChannels_.end())
        {
            RecordFailure("Echo worker received PacketReceived without an active session");
            return false;
        }
        if (packetType != psnr::core::NrPacketType{BenchmarkRequestPacketType})
        {
            RecordFailure("Echo worker received an unsupported packet type");
            return false;
        }
        if (payload.data == nullptr || payload.size != BenchmarkCanonicalPayloadBytes)
        {
            RecordFailure("Echo request payload must use the canonical 64-byte shape");
            return false;
        }

        BenchmarkProtocolCodec::CanonicalPayload canonicalPayload{};
        std::copy_n(payload.data, canonicalPayload.size(), canonicalPayload.begin());
        const BenchmarkPayload request = BenchmarkProtocolCodec::DecodeCanonical(canonicalPayload);
        if (request.protocolVersion != BenchmarkProtocolVersion || request.operation != BenchmarkOperation::Echo ||
            request.clientSendTimestampNanoseconds == 0 || request.serverReceivedTimestampNanoseconds != 0 ||
            request.serverResponsePreparedTimestampNanoseconds != 0 ||
            !BenchmarkProtocolCodec::HasDeterministicPadding(canonicalPayload))
        {
            RecordFailure("Echo request does not satisfy benchmark protocol v2");
            return false;
        }

        BenchmarkPayload response = request;
        response.serverReceivedTimestampNanoseconds = serverReceivedTimestampNanoseconds;
        response.serverResponsePreparedTimestampNanoseconds = SteadyNanosecondsNow();
        if (response.serverResponsePreparedTimestampNanoseconds < response.serverReceivedTimestampNanoseconds)
        {
            RecordFailure("Echo worker timestamps moved backwards");
            return false;
        }
        const BenchmarkProtocolCodec::CanonicalPayload responsePayload =
            BenchmarkProtocolCodec::EncodeCanonical(response);
        const psnr::runtime::NrByteView responseView{
            responsePayload.data(),
            static_cast<std::uint32_t>(responsePayload.size()),
        };

        const psnr::core::NrStatus submitStatus = gateway_.Submit(
            channelIterator->second, psnr::core::NrPacketType{BenchmarkResponsePacketType}, responseView);
        if (submitStatus.Failed())
        {
            const std::string error = StatusError("NrGateway::Submit", submitStatus);
            RecordFailure(error);
            return false;
        }
        return true;
    }

    void BenchmarkServerEchoWorker::RecordFailure(const std::string_view error) noexcept
    {
        try
        {
            {
                const std::lock_guard<std::mutex> lock(failureMutex_);
                if (!failure_.empty())
                {
                    return;
                }
                failure_ = error;
            }
            failed_.store(true, std::memory_order_release);
        }
        catch (...)
        {
            failed_.store(true, std::memory_order_release);
        }
    }
} // namespace psnr::benchmark
