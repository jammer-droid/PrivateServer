#ifndef NOMINMAX
#define NOMINMAX // Windows.h 에서 min, max 매크로 정의하지 못하게 막음(std와 충돌 방지)
#endif           // !NOMINMAX

#include <PrivateServer/NetworkRuntime/NrGateway.h>
#include <PrivateServer/NetworkRuntime/NrDiagnosticsConfig.h>
#include <PrivateServer/NetworkRuntime/NrPacketHeader.h>
#include <PrivateServer/NetworkRuntime/NrPacketType.h>
#include <PrivateServer/NetworkRuntime/NrServer.h>
#include <PrivateServer/NetworkRuntime/NrServerSnapshot.h>
#include <PrivateServer/NetworkRuntime/Version.h>

#include <WinSock2.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
    constexpr psnr::core::NrPacketType MoveInputPacketType{1};
    constexpr std::string_view DiagnosticsSchemaName = "psnr.network_runtime.diagnostics";
    constexpr std::uint64_t DiagnosticsSchemaVersion = 1;
    constexpr std::string_view JsonKeySchema = "schema";
    constexpr std::string_view JsonKeyVersion = "version";
    constexpr std::string_view JsonKeyType = "type";
    constexpr std::string_view JsonKeyClock = "clock";
    constexpr std::string_view JsonKeyMode = "mode";
    constexpr std::string_view JsonKeyAttempted = "attempted";
    constexpr std::string_view JsonKeyEnqueued = "enqueued";
    constexpr std::string_view JsonKeyConsumed = "consumed";
    constexpr std::string_view JsonKeyDroppedQueueFull = "droppedQueueFull";
    constexpr std::string_view JsonKeyDroppedSinkUnavailable = "droppedSinkUnavailable";
    constexpr std::string_view JsonKeyDiscardedAfterSinkFailure = "discardedAfterSinkFailure";
    constexpr std::string_view JsonKeyEventFlushSucceeded = "eventFlushSucceeded";

    using JsonObject = nlohmann::json;

    struct NrSmokeDiagnosticsOptions final
    {
        psnr::runtime::NrDiagnosticsMode mode = psnr::runtime::NrDiagnosticsMode::Disabled;
        std::string outputPath;
    };

    [[nodiscard]] const char* DiagnosticsModeName(const psnr::runtime::NrDiagnosticsMode mode) noexcept
    {
        switch (mode)
        {
        case psnr::runtime::NrDiagnosticsMode::Disabled:
            return "disabled";
        case psnr::runtime::NrDiagnosticsMode::Debug:
            return "debug";
        case psnr::runtime::NrDiagnosticsMode::Benchmark:
            return "benchmark";
        }

        return "unknown";
    }

    [[nodiscard]] bool ParseDiagnosticsOptions(const int argumentCount, char* arguments[],
                                               NrSmokeDiagnosticsOptions& outOptions)
    {
        if (argumentCount == 1)
        {
            return true;
        }

        if (argumentCount == 2)
        {
            const std::string_view modeArgument(arguments[1]);
            if (modeArgument == "disabled")
            {
                outOptions.mode = psnr::runtime::NrDiagnosticsMode::Disabled;
                return true;
            }
            if (modeArgument == "debug")
            {
                outOptions.mode = psnr::runtime::NrDiagnosticsMode::Debug;
                return true;
            }
        }
        else if (argumentCount == 3 && std::string_view(arguments[1]) == "benchmark")
        {
            const std::string_view outputPathArgument(arguments[2]);
            if (!outputPathArgument.empty() && outputPathArgument.size() <= std::numeric_limits<std::uint32_t>::max())
            {
                outOptions.mode = psnr::runtime::NrDiagnosticsMode::Benchmark;
                outOptions.outputPath.assign(outputPathArgument);
                return true;
            }
        }

        std::cout << "usage: PrivateServer.NetworkRuntime.Smoke.exe "
                     "[disabled|debug|benchmark <utf8-jsonl-path>]\n";
        return false;
    }

    void ApplyDiagnosticsOptions(const NrSmokeDiagnosticsOptions& options,
                                 psnr::runtime::NrServerConfig& config) noexcept
    {
        config.diagnostics.mode = options.mode;
        if (options.mode == psnr::runtime::NrDiagnosticsMode::Benchmark)
        {
            config.diagnostics.outputPath = psnr::runtime::NrUtf8View{
                options.outputPath.data(), static_cast<std::uint32_t>(options.outputPath.size())};
        }
    }

    [[nodiscard]] psnr::runtime::NrByteView MakeByteView(const std::span<const std::byte> bytes) noexcept
    {
        return psnr::runtime::NrByteView{bytes.data(), static_cast<std::uint32_t>(bytes.size())};
    }

    [[nodiscard]] psnr::runtime::NrSessionSendChannelView MakeChannelView(
        const std::span<const psnr::runtime::NrSessionSendChannel> channels) noexcept
    {
        return psnr::runtime::NrSessionSendChannelView{channels.data(), static_cast<std::uint32_t>(channels.size())};
    }

    class NrSmokeWinsockScope final
    {
    public:
        NrSmokeWinsockScope() noexcept = default;

        NrSmokeWinsockScope(const NrSmokeWinsockScope&) = delete;
        NrSmokeWinsockScope& operator=(const NrSmokeWinsockScope&) = delete;

        ~NrSmokeWinsockScope() noexcept
        {
            if (started_)
            {
                WSACleanup();
            }
        }

        [[nodiscard]] bool Start() noexcept
        {
            if (started_)
            {
                return true;
            }

            WSADATA data{};
            const int result = WSAStartup(MAKEWORD(2, 2), &data);
            if (result != 0)
            {
                std::cout << "WSAStartup failed: wsa=" << result << '\n';
                return false;
            }

            started_ = true;
            return true;
        }

    private:
        bool started_ = false;
    };

    class NrSmokeWorldEventConsumer final
    {
    public:
        explicit NrSmokeWorldEventConsumer(psnr::runtime::NrServer& server) noexcept
            : server_(&server)
        {
        }

        [[nodiscard]] bool DrainAvailable()
        {
            for (;;)
            {
                psnr::runtime::NrToWorldEvent event;
                const psnr::core::NrStatus popStatus = server_->TryPopToWorldEvent(&event);
                if (popStatus.ErrorCode() == psnr::core::NrErrorCode::QueueEmpty)
                {
                    return true;
                }
                if (popStatus.Failed())
                {
                    std::cout << "TryPopToWorldEvent failed: error=" << static_cast<int>(popStatus.ErrorCode()) << '\n';
                    return false;
                }

                switch (event.Kind())
                {
                case psnr::runtime::NrToWorldEventKind::SessionAccepted:
                {
                    psnr::runtime::NrSessionSendChannel sendChannel;
                    const psnr::core::NrStatus channelStatus = event.GetSendChannel(&sendChannel);
                    if (channelStatus.Failed())
                    {
                        std::cout << "SessionAccepted channel read failed: error="
                                  << static_cast<int>(channelStatus.ErrorCode()) << '\n';
                        return false;
                    }

                    if (!activeSessionKeys_.insert(event.SessionKey()).second)
                    {
                        std::cout << "duplicate SessionAccepted sessionKey=" << event.SessionKey() << '\n';
                        return false;
                    }

                    acceptedChannel_ = std::move(sendChannel);
                    lastAcceptedSessionKey_ = event.SessionKey();
                    ++acceptedCount_;
                    break;
                }
                case psnr::runtime::NrToWorldEventKind::SessionClosed:
                {
                    psnr::runtime::NrSessionEndReason endReason = psnr::runtime::NrSessionEndReason::None;
                    const psnr::core::NrStatus reasonStatus = event.GetEndReason(endReason);
                    if (reasonStatus.Failed() || endReason == psnr::runtime::NrSessionEndReason::None)
                    {
                        std::cout << "SessionClosed reason read failed: error="
                                  << static_cast<int>(reasonStatus.ErrorCode()) << '\n';
                        return false;
                    }

                    if (activeSessionKeys_.erase(event.SessionKey()) != 1)
                    {
                        std::cout << "SessionClosed arrived before SessionAccepted sessionKey=" << event.SessionKey()
                                  << '\n';
                        return false;
                    }

                    lastEndReason_ = endReason;
                    ++closedCount_;
                    break;
                }
                case psnr::runtime::NrToWorldEventKind::PacketReceived:
                {
                    psnr::core::NrPacketType packetType{};
                    psnr::runtime::NrByteView payload;
                    const psnr::core::NrStatus packetTypeStatus = event.GetPacketType(&packetType);
                    const psnr::core::NrStatus payloadStatus = event.GetPayload(&payload);
                    if (packetTypeStatus.Failed() || payloadStatus.Failed())
                    {
                        std::cout << "PacketReceived read failed: typeError="
                                  << static_cast<int>(packetTypeStatus.ErrorCode())
                                  << " payloadError=" << static_cast<int>(payloadStatus.ErrorCode()) << '\n';
                        return false;
                    }

                    if (activeSessionKeys_.find(event.SessionKey()) == activeSessionKeys_.end())
                    {
                        std::cout << "PacketReceived arrived outside Accepted/Closed lifetime sessionKey="
                                  << event.SessionKey() << '\n';
                        return false;
                    }

                    lastPacketType_ = packetType;
                    lastPayload_.clear();
                    if (payload.size != 0)
                    {
                        lastPayload_.assign(payload.data, payload.data + payload.size);
                    }
                    ++packetCount_;
                    break;
                }
                case psnr::runtime::NrToWorldEventKind::None:
                    std::cout << "unexpected to-world event kind=" << static_cast<int>(event.Kind()) << '\n';
                    return false;
                }
            }
        }

        [[nodiscard]] std::size_t AcceptedCount() const noexcept
        {
            return acceptedCount_;
        }

        [[nodiscard]] std::size_t ClosedCount() const noexcept
        {
            return closedCount_;
        }

        [[nodiscard]] std::size_t PacketCount() const noexcept
        {
            return packetCount_;
        }

        [[nodiscard]] std::size_t ActiveSessionCount() const noexcept
        {
            return activeSessionKeys_.size();
        }

        [[nodiscard]] psnr::core::NrPacketType LastPacketType() const noexcept
        {
            return lastPacketType_;
        }

        [[nodiscard]] psnr::core::NrStatus CreateGateway(psnr::runtime::NrGateway& outGateway) const noexcept
        {
            return server_ == nullptr ? psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::InvalidState)
                                      : server_->CreateGateway(&outGateway);
        }

        [[nodiscard]] psnr::runtime::NrSessionEndReason LastEndReason() const noexcept
        {
            return lastEndReason_;
        }

        [[nodiscard]] psnr::core::NrSessionKey LastAcceptedSessionKey() const noexcept
        {
            return lastAcceptedSessionKey_;
        }

        [[nodiscard]] const std::vector<std::byte>& LastPayload() const noexcept
        {
            return lastPayload_;
        }

        [[nodiscard]] psnr::runtime::NrSessionSendChannel AcceptedChannel() const noexcept
        {
            return acceptedChannel_;
        }

    private:
        psnr::runtime::NrServer* server_ = nullptr; // main의 server가 consumer보다 오래 유지된다.
        std::size_t acceptedCount_ = 0;
        std::size_t closedCount_ = 0;
        std::size_t packetCount_ = 0;
        psnr::runtime::NrSessionSendChannel acceptedChannel_;
        psnr::core::NrSessionKey lastAcceptedSessionKey_ = 0;
        psnr::core::NrPacketType lastPacketType_{};
        psnr::runtime::NrSessionEndReason lastEndReason_ = psnr::runtime::NrSessionEndReason::None;
        std::vector<std::byte> lastPayload_;
        std::unordered_set<psnr::core::NrSessionKey> activeSessionKeys_;
    };

    [[nodiscard]] bool PrintFailure(const char* operation, const psnr::core::NrStatus& status)
    {
        if (status.Succeeded())
        {
            return false;
        }

        std::cout << operation << " failed: error=" << static_cast<int>(status.ErrorCode())
                  << " native=" << status.NativeErrorCode() << '\n';
        return true;
    }

    [[nodiscard]] bool CaptureSnapshot(psnr::runtime::NrServer& server, psnr::runtime::NrServerSnapshot& outSnapshot)
    {
        return !PrintFailure("NrServer::CaptureSnapshot", server.CaptureSnapshot(&outSnapshot));
    }

    [[nodiscard]] bool VerifyDiagnosticsSnapshot(const NrSmokeDiagnosticsOptions& options,
                                                 const psnr::runtime::NrServerSnapshot& snapshot)
    {
        const psnr::runtime::NrServerDiagnosticsSnapshot diagnostics = snapshot.Diagnostics();
        const bool expectedEnabled = options.mode != psnr::runtime::NrDiagnosticsMode::Disabled;
        const bool hasLoss = diagnostics.droppedQueueFull != 0 || diagnostics.droppedSinkUnavailable != 0 ||
                             diagnostics.discardedAfterSinkFailure != 0;

        if (diagnostics.enabled != expectedEnabled || diagnostics.sinkFailed || hasLoss)
        {
            std::cout << "diagnostics health mismatch: mode=" << DiagnosticsModeName(options.mode)
                      << " enabled=" << diagnostics.enabled << " sinkFailed=" << diagnostics.sinkFailed
                      << " droppedQueueFull=" << diagnostics.droppedQueueFull
                      << " droppedSinkUnavailable=" << diagnostics.droppedSinkUnavailable
                      << " discardedAfterSinkFailure=" << diagnostics.discardedAfterSinkFailure << '\n';
            return false;
        }

        if (!expectedEnabled)
        {
            if (diagnostics.attempted != 0 || diagnostics.enqueued != 0 || diagnostics.consumed != 0)
            {
                std::cout << "disabled diagnostics changed counters: attempted=" << diagnostics.attempted
                          << " enqueued=" << diagnostics.enqueued << " consumed=" << diagnostics.consumed << '\n';
                return false;
            }
        }
        else if (diagnostics.attempted == 0 || diagnostics.enqueued != diagnostics.attempted ||
                 diagnostics.consumed != diagnostics.enqueued)
        {
            std::cout << "enabled diagnostics counters mismatch: attempted=" << diagnostics.attempted
                      << " enqueued=" << diagnostics.enqueued << " consumed=" << diagnostics.consumed << '\n';
            return false;
        }

        std::cout << "diagnostics evidence: mode=" << DiagnosticsModeName(options.mode)
                  << " attempted=" << diagnostics.attempted << " enqueued=" << diagnostics.enqueued
                  << " consumed=" << diagnostics.consumed << " droppedQueueFull=" << diagnostics.droppedQueueFull
                  << " droppedSinkUnavailable=" << diagnostics.droppedSinkUnavailable
                  << " discardedAfterSinkFailure=" << diagnostics.discardedAfterSinkFailure
                  << " sinkFailed=" << diagnostics.sinkFailed << '\n';
        return true;
    }

    [[nodiscard]] bool VerifyBenchmarkArtifact(const NrSmokeDiagnosticsOptions& options,
                                               const psnr::runtime::NrServerSnapshot& snapshot)
    {
        if (options.mode != psnr::runtime::NrDiagnosticsMode::Benchmark)
        {
            return true;
        }

        const std::u8string artifactPathUtf8(options.outputPath.cbegin(), options.outputPath.cend());
        const std::filesystem::path artifactPath(artifactPathUtf8);
        std::error_code fileSizeError;
        const std::uintmax_t artifactBytes = std::filesystem::file_size(artifactPath, fileSizeError);
        if (fileSizeError || artifactBytes == 0)
        {
            std::cout << "benchmark artifact size unavailable: path=" << options.outputPath
                      << " error=" << fileSizeError.value() << '\n';
            return false;
        }

        std::ifstream input(artifactPath, std::ios::binary);
        if (!input.is_open())
        {
            std::cout << "benchmark artifact open failed: path=" << options.outputPath << '\n';
            return false;
        }

        std::vector<std::string> lines;
        std::string line;
        while (std::getline(input, line))
        {
            lines.push_back(std::move(line));
        }
        if (input.bad())
        {
            std::cout << "benchmark artifact read failed: path=" << options.outputPath << '\n';
            return false;
        }

        const psnr::runtime::NrServerDiagnosticsSnapshot diagnostics = snapshot.Diagnostics();
        const std::size_t expectedLineCount = static_cast<std::size_t>(diagnostics.consumed) + 2;

        if (lines.size() != expectedLineCount)
        {
            std::cout << "benchmark artifact row mismatch: expectedLines=" << expectedLineCount
                      << " actualLines=" << lines.size() << '\n';
            return false;
        }

        JsonObject expectedRun = JsonObject::object();
        expectedRun[JsonKeySchema] = std::string(DiagnosticsSchemaName);
        expectedRun[JsonKeyVersion] = DiagnosticsSchemaVersion;
        expectedRun[JsonKeyType] = "run";
        expectedRun[JsonKeyClock] = "steady_ns";
        expectedRun[JsonKeyMode] = "benchmark";

        const JsonObject run = JsonObject::parse(lines.front(), nullptr, false);
        if (run.is_discarded() || !run.is_object() || run != expectedRun)
        {
            std::cout << "benchmark artifact run row mismatch\n";
            return false;
        }

        for (std::size_t index = 1; index + 1 < lines.size(); ++index)
        {
            const JsonObject event = JsonObject::parse(lines[index], nullptr, false);
            if (event.is_discarded() || !event.is_object() || !event.contains(JsonKeySchema) ||
                event[JsonKeySchema] != std::string(DiagnosticsSchemaName) || !event.contains(JsonKeyVersion) ||
                event[JsonKeyVersion] != DiagnosticsSchemaVersion || !event.contains(JsonKeyType) ||
                event[JsonKeyType] != std::string("event"))
            {
                std::cout << "benchmark artifact event row mismatch: index=" << index << '\n';
                return false;
            }
        }

        JsonObject expectedSummary = JsonObject::object();
        expectedSummary[JsonKeySchema] = std::string(DiagnosticsSchemaName);
        expectedSummary[JsonKeyVersion] = DiagnosticsSchemaVersion;
        expectedSummary[JsonKeyType] = "summary";
        expectedSummary[JsonKeyAttempted] = diagnostics.attempted;
        expectedSummary[JsonKeyEnqueued] = diagnostics.enqueued;
        expectedSummary[JsonKeyConsumed] = diagnostics.consumed;
        expectedSummary[JsonKeyDroppedQueueFull] = diagnostics.droppedQueueFull;
        expectedSummary[JsonKeyDroppedSinkUnavailable] = diagnostics.droppedSinkUnavailable;
        expectedSummary[JsonKeyDiscardedAfterSinkFailure] = diagnostics.discardedAfterSinkFailure;
        expectedSummary[JsonKeyEventFlushSucceeded] = true;

        const JsonObject summary = JsonObject::parse(lines.back(), nullptr, false);
        if (summary.is_discarded() || !summary.is_object() || summary != expectedSummary)
        {
            std::cout << "benchmark artifact summary mismatch\n";
            return false;
        }

        std::cout << "benchmark artifact evidence: path=" << options.outputPath << " bytes=" << artifactBytes
                  << " events=" << diagnostics.consumed << '\n';
        return true;
    }

    [[nodiscard]] bool VerifyReceivePressureSnapshot(const psnr::runtime::NrServerSnapshot& before,
                                                     const psnr::runtime::NrServerSnapshot& after,
                                                     const std::size_t toWorldEventCapacity)
    {
        const std::uint64_t closeCountBefore =
            before.PressureTransactionCount(psnr::runtime::NrPressureTransactionOutcome::ReceivePressureCloseCommitted);
        const std::uint64_t closeCountAfter =
            after.PressureTransactionCount(psnr::runtime::NrPressureTransactionOutcome::ReceivePressureCloseCommitted);
        const std::uint64_t rejectCountBefore = before.PressureTransactionCount(
            psnr::runtime::NrPressureTransactionOutcome::ToWorldPacketAdmissionRejected);
        const std::uint64_t rejectCountAfter =
            after.PressureTransactionCount(psnr::runtime::NrPressureTransactionOutcome::ToWorldPacketAdmissionRejected);

        if (closeCountAfter != closeCountBefore + 1 || rejectCountAfter <= rejectCountBefore ||
            after.ToWorldEventDepth() != 0 || after.ToWorldEventHighWatermark() != toWorldEventCapacity)
        {
            std::cout << "receive pressure snapshot mismatch: closeBefore=" << closeCountBefore
                      << " closeAfter=" << closeCountAfter << " rejectBefore=" << rejectCountBefore
                      << " rejectAfter=" << rejectCountAfter << " depth=" << after.ToWorldEventDepth()
                      << " highWatermark=" << after.ToWorldEventHighWatermark() << '\n';
            return false;
        }

        std::cout << "receive pressure snapshot observed. closeDelta=" << (closeCountAfter - closeCountBefore)
                  << " rejectDelta=" << (rejectCountAfter - rejectCountBefore)
                  << " toWorldHighWatermark=" << after.ToWorldEventHighWatermark() << '\n';
        return true;
    }

    [[nodiscard]] bool VerifySendPressureSnapshot(const psnr::runtime::NrServerSnapshot& before,
                                                  const psnr::runtime::NrServerSnapshot& after)
    {
        const std::uint64_t closeCountBefore =
            before.PressureTransactionCount(psnr::runtime::NrPressureTransactionOutcome::SendPressureCloseCommitted);
        const std::uint64_t closeCountAfter =
            after.PressureTransactionCount(psnr::runtime::NrPressureTransactionOutcome::SendPressureCloseCommitted);
        const std::uint64_t rejectCountBefore =
            before.PressureTransactionCount(psnr::runtime::NrPressureTransactionOutcome::SendAdmissionRejected);
        const std::uint64_t rejectCountAfter =
            after.PressureTransactionCount(psnr::runtime::NrPressureTransactionOutcome::SendAdmissionRejected);

        if (closeCountAfter != closeCountBefore + 1 || rejectCountAfter <= rejectCountBefore)
        {
            std::cout << "send pressure snapshot mismatch: closeBefore=" << closeCountBefore
                      << " closeAfter=" << closeCountAfter << " rejectBefore=" << rejectCountBefore
                      << " rejectAfter=" << rejectCountAfter << '\n';
            return false;
        }

        std::cout << "send pressure snapshot observed. closeDelta=" << (closeCountAfter - closeCountBefore)
                  << " rejectDelta=" << (rejectCountAfter - rejectCountBefore)
                  << " pendingSendIo=" << after.PendingSendIoCount() << '\n';
        return true;
    }

    [[nodiscard]] bool FindAvailableLoopbackPort(std::uint16_t& outPort)
    {
        SOCKET probeSocket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (probeSocket == INVALID_SOCKET)
        {
            std::cout << "probe socket failed: wsa=" << WSAGetLastError() << '\n';
            return false;
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;

        if (::bind(probeSocket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
        {
            std::cout << "probe bind failed: wsa=" << WSAGetLastError() << '\n';
            closesocket(probeSocket);
            return false;
        }

        int addressLength = sizeof(address);
        if (::getsockname(probeSocket, reinterpret_cast<sockaddr*>(&address), &addressLength) == SOCKET_ERROR)
        {
            std::cout << "probe getsockname failed: wsa=" << WSAGetLastError() << '\n';
            closesocket(probeSocket);
            return false;
        }

        outPort = ntohs(address.sin_port);
        closesocket(probeSocket);
        return outPort != 0;
    }

    [[nodiscard]] bool ConnectLoopbackClient(std::uint16_t port, SOCKET& outSocket, bool minimizeReceiveBuffer = false)
    {
        outSocket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (outSocket == INVALID_SOCKET)
        {
            std::cout << "socket failed: wsa=" << WSAGetLastError() << '\n';
            return false;
        }

        if (minimizeReceiveBuffer)
        {
            const int receiveBufferSize = 1024;
            if (::setsockopt(outSocket, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&receiveBufferSize),
                             sizeof(receiveBufferSize)) == SOCKET_ERROR)
            {
                std::cout << "setsockopt SO_RCVBUF failed: wsa=" << WSAGetLastError() << '\n';
                closesocket(outSocket);
                outSocket = INVALID_SOCKET;
                return false;
            }
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(port);

        if (::connect(outSocket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
        {
            std::cout << "connect failed: wsa=" << WSAGetLastError() << '\n';
            closesocket(outSocket);
            outSocket = INVALID_SOCKET;
            return false;
        }

        return true;
    }

    [[nodiscard]] std::vector<std::byte> MakeSmokePacket(psnr::core::NrPacketType packetType,
                                                         std::span<const std::byte> payload)
    {
        const std::uint16_t packetLength =
            static_cast<std::uint16_t>(psnr::core::NrPacketHeaderLength + payload.size());
        std::vector<std::byte> packet;
        packet.reserve(packetLength);
        packet.push_back(static_cast<std::byte>(packetLength & 0xFF));
        packet.push_back(static_cast<std::byte>((packetLength >> 8) & 0xFF));

        const std::uint16_t packetTypeValue = packetType.value;
        packet.push_back(static_cast<std::byte>(packetTypeValue & 0xFF));
        packet.push_back(static_cast<std::byte>((packetTypeValue >> 8) & 0xFF));

        packet.push_back(std::byte{1});
        packet.push_back(std::byte{0});
        packet.insert(packet.end(), payload.begin(), payload.end());
        return packet;
    }

    [[nodiscard]] bool SendAll(SOCKET socket, std::span<const std::byte> bytes)
    {
        std::size_t sentBytes = 0;
        while (sentBytes < bytes.size())
        {
            const int chunkSize = static_cast<int>(bytes.size() - sentBytes);
            const int result = ::send(socket, reinterpret_cast<const char*>(bytes.data() + sentBytes), chunkSize, 0);
            if (result == SOCKET_ERROR)
            {
                std::cout << "client send failed: wsa=" << WSAGetLastError() << '\n';
                return false;
            }

            if (result == 0)
            {
                std::cout << "client send returned zero bytes\n";
                return false;
            }

            sentBytes += static_cast<std::size_t>(result);
        }

        return true;
    }

    [[nodiscard]] bool ReceiveExact(SOCKET socket, std::span<std::byte> buffer)
    {
        constexpr int TimeoutMilliseconds = 5000;
        const int timeout = TimeoutMilliseconds;
        if (::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout)) ==
            SOCKET_ERROR)
        {
            std::cout << "setsockopt SO_RCVTIMEO failed: wsa=" << WSAGetLastError() << '\n';
            return false;
        }

        std::size_t receivedBytes = 0;
        while (receivedBytes < buffer.size())
        {
            const int chunkSize = static_cast<int>(buffer.size() - receivedBytes);
            const int result = ::recv(socket, reinterpret_cast<char*>(buffer.data() + receivedBytes), chunkSize, 0);
            if (result == SOCKET_ERROR)
            {
                std::cout << "client recv failed: wsa=" << WSAGetLastError() << '\n';
                return false;
            }

            if (result == 0)
            {
                std::cout << "client recv observed socket close\n";
                return false;
            }

            receivedBytes += static_cast<std::size_t>(result);
        }

        return true;
    }

    [[nodiscard]] bool WaitForAcceptedSession(NrSmokeWorldEventConsumer& worldEvents, std::size_t expectedCount = 1)
    {
        constexpr std::chrono::seconds Timeout(10);
        constexpr std::chrono::milliseconds PollInterval(10);
        const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + Timeout;

        while (std::chrono::steady_clock::now() < deadline)
        {
            if (!worldEvents.DrainAvailable())
            {
                return false;
            }
            if (worldEvents.AcceptedCount() >= expectedCount)
            {
                return true;
            }

            std::this_thread::sleep_for(PollInterval);
        }

        std::cout << "accepted session was not observed before timeout\n";
        return false;
    }

    [[nodiscard]] bool WaitForClosedSession(NrSmokeWorldEventConsumer& worldEvents, std::size_t expectedCount = 1)
    {
        constexpr auto Timeout = std::chrono::seconds(10);
        constexpr auto PollInterval = std::chrono::milliseconds(10);
        const auto deadline = std::chrono::steady_clock::now() + Timeout;

        while (std::chrono::steady_clock::now() < deadline)
        {
            if (!worldEvents.DrainAvailable())
            {
                return false;
            }
            if (worldEvents.ClosedCount() >= expectedCount)
            {
                return true;
            }

            std::this_thread::sleep_for(PollInterval);
        }

        std::cout << "closed session was not observed before timeout\n";
        return false;
    }

    [[nodiscard]] bool WaitForCurrentSendChannelClosed(const NrSmokeWorldEventConsumer& worldEvents)
    {
        constexpr std::chrono::seconds Timeout(10);
        constexpr std::chrono::milliseconds PollInterval(10);
        const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + Timeout;

        while (std::chrono::steady_clock::now() < deadline)
        {
            const psnr::runtime::NrSessionSendChannel sendChannel = worldEvents.AcceptedChannel();
            if (sendChannel.IsValid() && !sendChannel.IsOpen())
            {
                return true;
            }

            std::this_thread::sleep_for(PollInterval);
        }

        std::cout << "session send channel did not close before timeout\n";
        return false;
    }

    [[nodiscard]] bool SendSmokeRequest(SOCKET clientSocket)
    {
        const std::array<std::byte, 3> payload = {
            std::byte{0x10},
            std::byte{0x20},
            std::byte{0x30},
        };
        std::vector<std::byte> packet = MakeSmokePacket(MoveInputPacketType, std::span(payload));
        if (!SendAll(clientSocket, std::span<const std::byte>(packet)))
        {
            return false;
        }

        std::cout << "client sent framed MoveInput packet bytes=" << packet.size() << '\n';
        return true;
    }

    [[nodiscard]] bool SendWorldIngressPressureBurst(SOCKET clientSocket)
    {
        constexpr std::size_t FrameCount = 3;
        const std::array<std::byte, 1> payload = {std::byte{0x55}};
        std::vector<std::byte> burst;

        for (std::size_t index = 0; index < FrameCount; ++index)
        {
            std::vector<std::byte> packet = MakeSmokePacket(MoveInputPacketType, std::span(payload));
            burst.insert(burst.end(), packet.begin(), packet.end());
        }

        if (!SendAll(clientSocket, std::span<const std::byte>(burst)))
        {
            return false;
        }

        std::cout << "client sent WorldIngress pressure burst frames=" << FrameCount << " bytes=" << burst.size()
                  << '\n';
        return true;
    }

    [[nodiscard]] bool ReceiveSmokeResponse(SOCKET clientSocket)
    {
        const std::array<std::byte, 10> expected = {
            std::byte{0x0A}, std::byte{0x00}, // packet length
            std::byte{0x01}, std::byte{0x00}, // packet type
            std::byte{0x01}, std::byte{0x00}, // version, flags
            std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04},
        };
        std::array<std::byte, expected.size()> received{};
        if (!ReceiveExact(clientSocket, std::span<std::byte>(received)))
        {
            return false;
        }

        if (received != expected)
        {
            std::cout << "unexpected response bytes\n";
            return false;
        }

        std::cout << "client received response bytes=" << received.size() << '\n';
        return true;
    }

    [[nodiscard]] bool SubmitSmokePayload(const NrSmokeWorldEventConsumer& worldEvents)
    {
        psnr::runtime::NrSessionSendChannel sendChannel = worldEvents.AcceptedChannel();
        if (!sendChannel.IsValid() || !sendChannel.IsOpen())
        {
            std::cout << "accepted send channel is not open\n";
            return false;
        }

        psnr::runtime::NrGateway gateway;
        const psnr::core::NrStatus gatewayCreateStatus = worldEvents.CreateGateway(gateway);
        if (PrintFailure("NrServer::CreateGateway", gatewayCreateStatus))
        {
            return false;
        }

        const std::array<std::byte, 4> payload = {
            std::byte{0x01},
            std::byte{0x02},
            std::byte{0x03},
            std::byte{0x04},
        };

        const psnr::core::NrStatus submitStatus =
            gateway.Submit(sendChannel, MoveInputPacketType, MakeByteView(std::span<const std::byte>(payload)));
        if (PrintFailure("NrGateway::Submit", submitStatus))
        {
            return false;
        }

        std::cout << "NrGateway::Submit succeeded\n";
        return true;
    }

    [[nodiscard]] bool SubmitPartialBroadcast(const NrSmokeWorldEventConsumer& worldEvents)
    {
        const std::array<psnr::runtime::NrSessionSendChannel, 2> channels = {
            worldEvents.AcceptedChannel(),
            psnr::runtime::NrSessionSendChannel{},
        };
        psnr::runtime::NrGateway gateway;
        if (PrintFailure("NrServer::CreateGateway", worldEvents.CreateGateway(gateway)))
        {
            return false;
        }

        const std::array<std::byte, 4> payload = {
            std::byte{0x01},
            std::byte{0x02},
            std::byte{0x03},
            std::byte{0x04},
        };
        psnr::runtime::NrGatewaySendReport report;
        const psnr::core::NrStatus submitStatus =
            gateway.SubmitMany(MakeChannelView(std::span<const psnr::runtime::NrSessionSendChannel>(channels)),
                               MoveInputPacketType, MakeByteView(std::span<const std::byte>(payload)), report);
        if (PrintFailure("NrGateway::SubmitMany", submitStatus))
        {
            return false;
        }
        if (report.attempted != 2 || report.accepted != 1 || report.rejected != 1)
        {
            std::cout << "unexpected SubmitMany report attempted=" << report.attempted
                      << " accepted=" << report.accepted << " rejected=" << report.rejected << '\n';
            return false;
        }

        std::cout << "NrGateway::SubmitMany preserved partial acceptance\n";
        return true;
    }

    [[nodiscard]] bool SubmitDuplicateBroadcast(const NrSmokeWorldEventConsumer& worldEvents)
    {
        const psnr::runtime::NrSessionSendChannel sendChannel = worldEvents.AcceptedChannel();
        const std::array<psnr::runtime::NrSessionSendChannel, 2> channels = {sendChannel, sendChannel};
        psnr::runtime::NrGateway gateway;
        if (PrintFailure("NrServer::CreateGateway", worldEvents.CreateGateway(gateway)))
        {
            return false;
        }

        const std::array<std::byte, 4> payload = {
            std::byte{0x01},
            std::byte{0x02},
            std::byte{0x03},
            std::byte{0x04},
        };
        psnr::runtime::NrGatewaySendReport report;
        const psnr::core::NrStatus submitStatus =
            gateway.SubmitMany(MakeChannelView(std::span<const psnr::runtime::NrSessionSendChannel>(channels)),
                               MoveInputPacketType, MakeByteView(std::span<const std::byte>(payload)), report);
        if (PrintFailure("NrGateway::SubmitMany duplicate", submitStatus))
        {
            return false;
        }
        if (report.attempted != 2 || report.accepted != 2 || report.rejected != 0)
        {
            std::cout << "unexpected duplicate SubmitMany report attempted=" << report.attempted
                      << " accepted=" << report.accepted << " rejected=" << report.rejected << '\n';
            return false;
        }

        std::cout << "NrGateway::SubmitMany treated duplicate recipients independently\n";
        return true;
    }

    [[nodiscard]] bool RunBidirectionalExchange(SOCKET clientSocket, NrSmokeWorldEventConsumer& worldEvents)
    {
        const std::size_t initialPacketCount = worldEvents.PacketCount();

        if (!SendSmokeRequest(clientSocket))
        {
            return false;
        }

        constexpr auto Timeout = std::chrono::seconds(10);
        constexpr auto PollInterval = std::chrono::milliseconds(10);
        const auto deadline = std::chrono::steady_clock::now() + Timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (!worldEvents.DrainAvailable())
            {
                return false;
            }
            if (worldEvents.PacketCount() > initialPacketCount)
            {
                break;
            }

            std::this_thread::sleep_for(PollInterval);
        }

        if (worldEvents.PacketCount() <= initialPacketCount)
        {
            std::cout << "PacketReceived event was not observed before timeout\n";
            return false;
        }

        if (worldEvents.LastPacketType() != MoveInputPacketType)
        {
            std::cout << "unexpected packet type=" << static_cast<std::uint16_t>(worldEvents.LastPacketType().value)
                      << '\n';
            return false;
        }

        const std::array<std::byte, 3> expectedPayload = {
            std::byte{0x10},
            std::byte{0x20},
            std::byte{0x30},
        };
        const std::vector<std::byte>& observedPayload = worldEvents.LastPayload();
        if (observedPayload.size() != expectedPayload.size() ||
            !std::equal(observedPayload.begin(), observedPayload.end(), expectedPayload.begin()))
        {
            std::cout << "unexpected PacketReceived payload\n";
            return false;
        }

        std::cout << "world drained PacketReceived. count=" << worldEvents.PacketCount() << '\n';

        if (!SubmitSmokePayload(worldEvents))
        {
            return false;
        }

        if (!ReceiveSmokeResponse(clientSocket) || !SubmitPartialBroadcast(worldEvents) ||
            !ReceiveSmokeResponse(clientSocket) || !SubmitDuplicateBroadcast(worldEvents))
        {
            return false;
        }

        return ReceiveSmokeResponse(clientSocket) && ReceiveSmokeResponse(clientSocket);
    }

    [[nodiscard]] bool SubmitSlowClientPressureBurst(const NrSmokeWorldEventConsumer& worldEvents)
    {
        psnr::runtime::NrSessionSendChannel sendChannel = worldEvents.AcceptedChannel();
        if (!sendChannel.IsValid() || !sendChannel.IsOpen())
        {
            std::cout << "slow client send channel is not open\n";
            return false;
        }

        psnr::runtime::NrGateway gateway;
        const psnr::core::NrStatus gatewayCreateStatus = worldEvents.CreateGateway(gateway);
        if (PrintFailure("NrServer::CreateGateway", gatewayCreateStatus))
        {
            return false;
        }

        constexpr std::size_t PayloadSize = 8192 - 6;
        constexpr std::size_t SubmitAttempts = 32;
        std::vector<std::byte> payload(PayloadSize, std::byte{0x5A});

        std::size_t immediateAccepted = 0;
        std::size_t immediateRejected = 0;
        for (std::size_t index = 0; index < SubmitAttempts; ++index)
        {
            const psnr::core::NrStatus submitStatus =
                gateway.Submit(sendChannel, MoveInputPacketType, MakeByteView(std::span<const std::byte>(payload)));
            if (submitStatus.Succeeded())
            {
                ++immediateAccepted;
                continue;
            }

            ++immediateRejected;
            break;
        }

        std::cout << "slow client pressure burst submitted. accepted=" << immediateAccepted
                  << " rejected=" << immediateRejected << '\n';
        return immediateAccepted >= 2;
    }

    [[nodiscard]] bool VerifyClosedChannelRejectsSubmit(const NrSmokeWorldEventConsumer& worldEvents,
                                                        psnr::runtime::NrGateway& gateway)
    {
        psnr::runtime::NrSessionSendChannel sendChannel = worldEvents.AcceptedChannel();
        const std::array<std::byte, 1> payload = {std::byte{0x7F}};
        const psnr::core::NrStatus submitStatus =
            gateway.Submit(sendChannel, MoveInputPacketType, MakeByteView(std::span<const std::byte>(payload)));
        if (submitStatus.Succeeded())
        {
            std::cout << "closed session channel accepted submit unexpectedly\n";
            return false;
        }

        std::cout << "closed session channel rejected submit. error=" << static_cast<int>(submitStatus.ErrorCode())
                  << '\n';
        return true;
    }

    [[nodiscard]] bool VerifyClosedChannelRejectsSubmit(const NrSmokeWorldEventConsumer& worldEvents)
    {
        psnr::runtime::NrGateway gateway;
        return !PrintFailure("NrServer::CreateGateway", worldEvents.CreateGateway(gateway)) &&
               VerifyClosedChannelRejectsSubmit(worldEvents, gateway);
    }

    [[nodiscard]] bool VerifyCrossServerAndDestroyedServerHandles(const NrSmokeWorldEventConsumer& primaryWorldEvents)
    {
        psnr::runtime::NrGateway primaryGateway;
        if (PrintFailure("primary NrServer::CreateGateway", primaryWorldEvents.CreateGateway(primaryGateway)))
        {
            return false;
        }

        psnr::runtime::NrGateway staleGateway;
        psnr::runtime::NrSessionSendChannel staleChannel;
        {
            std::uint16_t secondaryPort = 0;
            if (!FindAvailableLoopbackPort(secondaryPort))
            {
                return false;
            }

            psnr::runtime::NrServerConfig secondaryConfig;
            secondaryConfig.bindEndpoint = psnr::runtime::NrEndpoint{
                psnr::runtime::NrEndpointAddressType::IPv4, psnr::runtime::NrIPv4Address::Loopback(), secondaryPort};
            secondaryConfig.pendingSendQueueCapacity = 4;
            secondaryConfig.toWorldEventCapacity = 2;

            psnr::runtime::NrServer secondaryServer;
            if (PrintFailure("secondary NrServer::Create",
                             psnr::runtime::NrServer::Create(secondaryConfig, &secondaryServer)) ||
                PrintFailure("secondary NrServer::Start", secondaryServer.Start()))
            {
                static_cast<void>(secondaryServer.Shutdown());
                return false;
            }

            NrSmokeWorldEventConsumer secondaryWorldEvents(secondaryServer);
            SOCKET secondaryClientSocket = INVALID_SOCKET;
            if (!ConnectLoopbackClient(secondaryPort, secondaryClientSocket) ||
                !WaitForAcceptedSession(secondaryWorldEvents))
            {
                if (secondaryClientSocket != INVALID_SOCKET)
                {
                    closesocket(secondaryClientSocket);
                }
                static_cast<void>(secondaryServer.RequestStop());
                static_cast<void>(secondaryServer.Shutdown());
                return false;
            }

            staleChannel = secondaryWorldEvents.AcceptedChannel();
            if (PrintFailure("secondary NrServer::CreateGateway", secondaryServer.CreateGateway(&staleGateway)))
            {
                closesocket(secondaryClientSocket);
                static_cast<void>(secondaryServer.RequestStop());
                static_cast<void>(secondaryServer.Shutdown());
                return false;
            }

            const std::array<std::byte, 1> payload = {std::byte{0x7F}};
            const psnr::core::NrStatus crossServerStatus = primaryGateway.Submit(
                staleChannel, MoveInputPacketType, MakeByteView(std::span<const std::byte>(payload)));
            if (crossServerStatus.ErrorCode() != psnr::core::NrErrorCode::InvalidState)
            {
                std::cout << "cross-server channel submit was not rejected: error="
                          << static_cast<int>(crossServerStatus.ErrorCode()) << '\n';
                closesocket(secondaryClientSocket);
                static_cast<void>(secondaryServer.RequestStop());
                static_cast<void>(secondaryServer.Shutdown());
                return false;
            }

            if (PrintFailure("secondary NrServer::RequestStop", secondaryServer.RequestStop()) ||
                PrintFailure("secondary NrServer::Shutdown", secondaryServer.Shutdown()))
            {
                closesocket(secondaryClientSocket);
                return false;
            }
            closesocket(secondaryClientSocket);

            if (!staleChannel.IsValid() || staleChannel.IsOpen())
            {
                std::cout << "secondary channel did not become a closed stale handle\n";
                return false;
            }
        }

        const std::array<std::byte, 1> payload = {std::byte{0x7F}};
        const psnr::core::NrStatus staleStatus =
            staleGateway.Submit(staleChannel, MoveInputPacketType, MakeByteView(std::span<const std::byte>(payload)));
        if (staleStatus.ErrorCode() != psnr::core::NrErrorCode::InvalidState)
        {
            std::cout << "destroyed-server Gateway submit was not rejected: error="
                      << static_cast<int>(staleStatus.ErrorCode()) << '\n';
            return false;
        }

        std::cout << "cross-server and destroyed-server handles rejected submit safely\n";
        return true;
    }
} // namespace

int main(const int argumentCount, char* arguments[])
{
    NrSmokeDiagnosticsOptions diagnosticsOptions;
    if (!ParseDiagnosticsOptions(argumentCount, arguments, diagnosticsOptions))
    {
        return 1;
    }

    std::cout << "NetworkRuntime Version: " << nr_get_version() << '\n';
    std::cout << "Diagnostics mode: " << DiagnosticsModeName(diagnosticsOptions.mode) << '\n';

    NrSmokeWinsockScope winsockScope;
    if (!winsockScope.Start())
    {
        return 1;
    }

    std::uint16_t listenPort = 0;
    if (!FindAvailableLoopbackPort(listenPort))
    {
        return 1;
    }

    psnr::runtime::NrServerConfig config;
    config.bindEndpoint = psnr::runtime::NrEndpoint{psnr::runtime::NrEndpointAddressType::IPv4,
                                                    psnr::runtime::NrIPv4Address::Loopback(), listenPort};
    // Keep enough room for the duplicate-recipient delivery check while the later
    // 32-submit burst still exercises pending-send pressure and close behavior.
    config.pendingSendQueueCapacity = 4;
    config.toWorldEventCapacity = 2;
    ApplyDiagnosticsOptions(diagnosticsOptions, config);

    psnr::runtime::NrServer server;
    const psnr::core::NrStatus createStatus = psnr::runtime::NrServer::Create(config, &server);
    if (PrintFailure("NrServer::Create", createStatus))
    {
        return 1;
    }

    NrSmokeWorldEventConsumer worldEvents(server);

    const psnr::core::NrStatus startStatus = server.Start();
    if (PrintFailure("NrServer::Start", startStatus))
    {
        static_cast<void>(server.Shutdown());
        return 1;
    }

    SOCKET clientSocket = INVALID_SOCKET;
    if (!ConnectLoopbackClient(config.bindEndpoint.port, clientSocket))
    {
        static_cast<void>(server.RequestStop());
        static_cast<void>(server.Shutdown());
        return 1;
    }

    if (!WaitForAcceptedSession(worldEvents))
    {
        closesocket(clientSocket);
        static_cast<void>(server.RequestStop());
        static_cast<void>(server.Shutdown());
        return 1;
    }

    if (!RunBidirectionalExchange(clientSocket, worldEvents))
    {
        closesocket(clientSocket);
        static_cast<void>(server.RequestStop());
        static_cast<void>(server.Shutdown());
        return 1;
    }

    if (::shutdown(clientSocket, SD_SEND) == SOCKET_ERROR)
    {
        std::cout << "client shutdown failed: wsa=" << WSAGetLastError() << '\n';
        closesocket(clientSocket);
        static_cast<void>(server.RequestStop());
        static_cast<void>(server.Shutdown());
        return 1;
    }

    if (!WaitForClosedSession(worldEvents))
    {
        closesocket(clientSocket);
        static_cast<void>(server.RequestStop());
        static_cast<void>(server.Shutdown());
        return 1;
    }

    closesocket(clientSocket);

    SOCKET slowClientSocket = INVALID_SOCKET;
    if (!ConnectLoopbackClient(config.bindEndpoint.port, slowClientSocket, true))
    {
        static_cast<void>(server.RequestStop());
        static_cast<void>(server.Shutdown());
        return 1;
    }

    if (!WaitForAcceptedSession(worldEvents, 2))
    {
        closesocket(slowClientSocket);
        static_cast<void>(server.RequestStop());
        static_cast<void>(server.Shutdown());
        return 1;
    }

    psnr::runtime::NrServerSnapshot beforeSendPressureSnapshot;
    if (!CaptureSnapshot(server, beforeSendPressureSnapshot))
    {
        closesocket(slowClientSocket);
        static_cast<void>(server.RequestStop());
        static_cast<void>(server.Shutdown());
        return 1;
    }

    if (!SubmitSlowClientPressureBurst(worldEvents))
    {
        closesocket(slowClientSocket);
        static_cast<void>(server.RequestStop());
        static_cast<void>(server.Shutdown());
        return 1;
    }

    if (!WaitForClosedSession(worldEvents, 2))
    {
        closesocket(slowClientSocket);
        static_cast<void>(server.RequestStop());
        static_cast<void>(server.Shutdown());
        return 1;
    }

    if (worldEvents.LastEndReason() != psnr::runtime::NrSessionEndReason::SendPressure)
    {
        std::cout << "slow client pressure close reported unexpected reason="
                  << static_cast<int>(worldEvents.LastEndReason()) << '\n';
        closesocket(slowClientSocket);
        static_cast<void>(server.RequestStop());
        static_cast<void>(server.Shutdown());
        return 1;
    }

    psnr::runtime::NrServerSnapshot afterSendPressureSnapshot;
    if (!CaptureSnapshot(server, afterSendPressureSnapshot) ||
        !VerifySendPressureSnapshot(beforeSendPressureSnapshot, afterSendPressureSnapshot))
    {
        closesocket(slowClientSocket);
        static_cast<void>(server.RequestStop());
        static_cast<void>(server.Shutdown());
        return 1;
    }

    if (::shutdown(slowClientSocket, SD_SEND) == SOCKET_ERROR)
    {
        std::cout << "slow client shutdown failed: wsa=" << WSAGetLastError() << '\n';
        closesocket(slowClientSocket);
        static_cast<void>(server.RequestStop());
        static_cast<void>(server.Shutdown());
        return 1;
    }

    if (!VerifyClosedChannelRejectsSubmit(worldEvents))
    {
        closesocket(slowClientSocket);
        static_cast<void>(server.RequestStop());
        static_cast<void>(server.Shutdown());
        return 1;
    }

    closesocket(slowClientSocket);

    SOCKET healthyClientSocket = INVALID_SOCKET;
    if (!ConnectLoopbackClient(config.bindEndpoint.port, healthyClientSocket))
    {
        static_cast<void>(server.RequestStop());
        static_cast<void>(server.Shutdown());
        return 1;
    }

    if (!WaitForAcceptedSession(worldEvents, 3))
    {
        closesocket(healthyClientSocket);
        static_cast<void>(server.RequestStop());
        static_cast<void>(server.Shutdown());
        return 1;
    }

    if (!RunBidirectionalExchange(healthyClientSocket, worldEvents))
    {
        closesocket(healthyClientSocket);
        static_cast<void>(server.RequestStop());
        static_cast<void>(server.Shutdown());
        return 1;
    }

    if (::shutdown(healthyClientSocket, SD_SEND) == SOCKET_ERROR)
    {
        std::cout << "healthy client shutdown failed: wsa=" << WSAGetLastError() << '\n';
        closesocket(healthyClientSocket);
        static_cast<void>(server.RequestStop());
        static_cast<void>(server.Shutdown());
        return 1;
    }

    if (!WaitForClosedSession(worldEvents, 3))
    {
        closesocket(healthyClientSocket);
        static_cast<void>(server.RequestStop());
        static_cast<void>(server.Shutdown());
        return 1;
    }

    if (worldEvents.ActiveSessionCount() != 0)
    {
        std::cout << "to-world lifecycle smoke left active sessions=" << worldEvents.ActiveSessionCount() << '\n';
        closesocket(healthyClientSocket);
        static_cast<void>(server.RequestStop());
        static_cast<void>(server.Shutdown());
        return 1;
    }

    closesocket(healthyClientSocket);

    SOCKET closeRequestClientSocket = INVALID_SOCKET;
    if (!ConnectLoopbackClient(config.bindEndpoint.port, closeRequestClientSocket))
    {
        static_cast<void>(server.RequestStop());
        static_cast<void>(server.Shutdown());
        return 1;
    }

    if (!WaitForAcceptedSession(worldEvents, 4))
    {
        closesocket(closeRequestClientSocket);
        static_cast<void>(server.RequestStop());
        static_cast<void>(server.Shutdown());
        return 1;
    }

    const psnr::core::NrStatus closeRequestStatus = server.RequestSessionClose(
        worldEvents.LastAcceptedSessionKey(), psnr::runtime::NrSessionCloseRequestReason::ApplicationRequested);
    if (PrintFailure("NrServer::RequestSessionClose", closeRequestStatus))
    {
        closesocket(closeRequestClientSocket);
        static_cast<void>(server.RequestStop());
        static_cast<void>(server.Shutdown());
        return 1;
    }

    if (!WaitForClosedSession(worldEvents, 4))
    {
        closesocket(closeRequestClientSocket);
        static_cast<void>(server.RequestStop());
        static_cast<void>(server.Shutdown());
        return 1;
    }

    if (worldEvents.LastEndReason() != psnr::runtime::NrSessionEndReason::ApplicationRequested)
    {
        std::cout << "requested session close reported unexpected reason="
                  << static_cast<int>(worldEvents.LastEndReason()) << '\n';
        closesocket(closeRequestClientSocket);
        static_cast<void>(server.RequestStop());
        static_cast<void>(server.Shutdown());
        return 1;
    }

    if (!VerifyClosedChannelRejectsSubmit(worldEvents))
    {
        closesocket(closeRequestClientSocket);
        static_cast<void>(server.RequestStop());
        static_cast<void>(server.Shutdown());
        return 1;
    }

    closesocket(closeRequestClientSocket);

    SOCKET pressureClientSocket = INVALID_SOCKET;
    if (!ConnectLoopbackClient(config.bindEndpoint.port, pressureClientSocket))
    {
        static_cast<void>(server.RequestStop());
        static_cast<void>(server.Shutdown());
        return 1;
    }

    if (!WaitForAcceptedSession(worldEvents, 5))
    {
        closesocket(pressureClientSocket);
        static_cast<void>(server.RequestStop());
        static_cast<void>(server.Shutdown());
        return 1;
    }

    const std::size_t packetCountBeforePressure = worldEvents.PacketCount();
    psnr::runtime::NrServerSnapshot beforeReceivePressureSnapshot;
    if (!CaptureSnapshot(server, beforeReceivePressureSnapshot))
    {
        closesocket(pressureClientSocket);
        static_cast<void>(server.RequestStop());
        static_cast<void>(server.Shutdown());
        return 1;
    }

    if (!SendWorldIngressPressureBurst(pressureClientSocket))
    {
        closesocket(pressureClientSocket);
        static_cast<void>(server.RequestStop());
        static_cast<void>(server.Shutdown());
        return 1;
    }

    if (!WaitForCurrentSendChannelClosed(worldEvents))
    {
        closesocket(pressureClientSocket);
        static_cast<void>(server.RequestStop());
        static_cast<void>(server.Shutdown());
        return 1;
    }

    if (!WaitForClosedSession(worldEvents, 5))
    {
        closesocket(pressureClientSocket);
        static_cast<void>(server.RequestStop());
        static_cast<void>(server.Shutdown());
        return 1;
    }

    if (worldEvents.LastEndReason() != psnr::runtime::NrSessionEndReason::ReceivePressure)
    {
        std::cout << "WorldIngress pressure close reported unexpected reason="
                  << static_cast<int>(worldEvents.LastEndReason()) << '\n';
        closesocket(pressureClientSocket);
        static_cast<void>(server.RequestStop());
        static_cast<void>(server.Shutdown());
        return 1;
    }

    if (worldEvents.PacketCount() != packetCountBeforePressure + config.toWorldEventCapacity)
    {
        std::cout << "WorldIngress pressure published unexpected packet count="
                  << (worldEvents.PacketCount() - packetCountBeforePressure) << '\n';
        closesocket(pressureClientSocket);
        static_cast<void>(server.RequestStop());
        static_cast<void>(server.Shutdown());
        return 1;
    }

    psnr::runtime::NrServerSnapshot afterReceivePressureSnapshot;
    if (!CaptureSnapshot(server, afterReceivePressureSnapshot) ||
        !VerifyReceivePressureSnapshot(beforeReceivePressureSnapshot, afterReceivePressureSnapshot,
                                       config.toWorldEventCapacity))
    {
        closesocket(pressureClientSocket);
        static_cast<void>(server.RequestStop());
        static_cast<void>(server.Shutdown());
        return 1;
    }

    if (!VerifyClosedChannelRejectsSubmit(worldEvents))
    {
        closesocket(pressureClientSocket);
        static_cast<void>(server.RequestStop());
        static_cast<void>(server.Shutdown());
        return 1;
    }

    closesocket(pressureClientSocket);

    SOCKET shutdownClientSocket = INVALID_SOCKET;
    if (!ConnectLoopbackClient(config.bindEndpoint.port, shutdownClientSocket))
    {
        static_cast<void>(server.RequestStop());
        static_cast<void>(server.Shutdown());
        return 1;
    }

    if (!WaitForAcceptedSession(worldEvents, 6))
    {
        closesocket(shutdownClientSocket);
        static_cast<void>(server.RequestStop());
        static_cast<void>(server.Shutdown());
        return 1;
    }

    psnr::runtime::NrGateway shutdownGateway;
    if (PrintFailure("NrServer::CreateGateway", worldEvents.CreateGateway(shutdownGateway)))
    {
        closesocket(shutdownClientSocket);
        static_cast<void>(server.RequestStop());
        static_cast<void>(server.Shutdown());
        return 1;
    }

    if (!VerifyCrossServerAndDestroyedServerHandles(worldEvents))
    {
        closesocket(shutdownClientSocket);
        static_cast<void>(server.RequestStop());
        static_cast<void>(server.Shutdown());
        return 1;
    }

    const psnr::core::NrStatus stopStatus = server.RequestStop();
    if (PrintFailure("NrServer::RequestStop", stopStatus))
    {
        closesocket(shutdownClientSocket);
        static_cast<void>(server.Shutdown());
        return 1;
    }

    const psnr::core::NrStatus shutdownStatus = server.Shutdown();
    if (PrintFailure("NrServer::Shutdown", shutdownStatus))
    {
        closesocket(shutdownClientSocket);
        return 1;
    }

    if (!WaitForClosedSession(worldEvents, 6))
    {
        closesocket(shutdownClientSocket);
        return 1;
    }

    if (worldEvents.LastEndReason() != psnr::runtime::NrSessionEndReason::ServerStopping)
    {
        std::cout << "shutdown session closed with unexpected reason=" << static_cast<int>(worldEvents.LastEndReason())
                  << '\n';
        closesocket(shutdownClientSocket);
        return 1;
    }

    if (worldEvents.ActiveSessionCount() != 0)
    {
        std::cout << "shutdown lifecycle left active sessions=" << worldEvents.ActiveSessionCount() << '\n';
        closesocket(shutdownClientSocket);
        return 1;
    }

    if (!VerifyClosedChannelRejectsSubmit(worldEvents, shutdownGateway))
    {
        closesocket(shutdownClientSocket);
        return 1;
    }

    closesocket(shutdownClientSocket);

    psnr::runtime::NrServerSnapshot finalSnapshot;
    if (!CaptureSnapshot(server, finalSnapshot) || !VerifyDiagnosticsSnapshot(diagnosticsOptions, finalSnapshot) ||
        !VerifyBenchmarkArtifact(diagnosticsOptions, finalSnapshot))
    {
        return 1;
    }

    std::cout << "NrServer lifecycle smoke succeeded. accepted=" << worldEvents.AcceptedCount()
              << " closed=" << worldEvents.ClosedCount() << '\n';

    return 0;
}
