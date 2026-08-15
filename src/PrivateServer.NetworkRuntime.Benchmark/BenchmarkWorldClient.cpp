#include "BenchmarkWorldClient.h"

#include "BenchmarkClientTransport.h"
#include "ControlStateCommand.h"
#include "ControlledEntityRebind.h"
#include "EntitySpawn.h"
#include "JoinWorldRequest.h"
#include "RoundResult.h"
#include "WorldPacketTypes.h"
#include "WorldReady.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace psnr::benchmark
{
    namespace
    {
        class BenchmarkWorldClientHelper final
        {
        public:
            [[nodiscard]] static std::string DecodePacket(
                const psnr::runtime::NrClientEvent& event,
                std::vector<psnr::world::protocol::v2::EntitySpawn>* const outSpawns,
                psnr::world::protocol::v2::WorldReady* const outReady, bool* const outReadyReceived)
            {
                psnr::core::NrPacketType packetType;
                psnr::runtime::NrByteView payloadView;
                const psnr::core::NrStatus packetTypeStatus = event.GetPacketType(&packetType);
                const psnr::core::NrStatus payloadStatus = event.GetPayload(&payloadView);
                if (packetTypeStatus.Failed() || payloadStatus.Failed() || payloadView.data == nullptr)
                {
                    return "World client received invalid packet metadata";
                }

                const std::span<const std::byte> payload{payloadView.data, payloadView.size};
                if (packetType.value == static_cast<std::uint16_t>(psnr::world::protocol::S2CPacketType::EntitySpawn))
                {
                    psnr::world::protocol::v2::EntitySpawn spawn;
                    if (psnr::world::protocol::v2::EntitySpawn::Decode(payload, &spawn) !=
                        psnr::world::protocol::WorldProtocolError::Success)
                    {
                        return "World client failed to decode EntitySpawn";
                    }
                    outSpawns->push_back(spawn);
                }
                else if (packetType.value ==
                         static_cast<std::uint16_t>(psnr::world::protocol::S2CPacketType::WorldReady))
                {
                    if (psnr::world::protocol::v2::WorldReady::Decode(payload, outReady) !=
                        psnr::world::protocol::WorldProtocolError::Success)
                    {
                        return "World client failed to decode WorldReady";
                    }
                    *outReadyReceived = true;
                }
                return {};
            }

            [[nodiscard]] static bool ContainsControlledEntity(
                const std::vector<psnr::world::protocol::v2::EntitySpawn>& spawns,
                const psnr::world::protocol::v2::WorldReady& ready) noexcept
            {
                for (const psnr::world::protocol::v2::EntitySpawn& spawn : spawns)
                {
                    if (spawn.baseline.entityId == ready.controlledEntityId &&
                        spawn.baseline.generation == ready.controlledEntityGeneration && spawn.playerId == ready.playerId)
                    {
                        return true;
                    }
                }
                return false;
            }

            [[nodiscard]] static std::string TryDecodeRoundResult(
                const psnr::runtime::NrClientEvent& event, psnr::world::protocol::v2::RoundResult* const outResult,
                bool* const outReceived)
            {
                psnr::core::NrPacketType packetType;
                psnr::runtime::NrByteView payloadView;
                const psnr::core::NrStatus packetTypeStatus = event.GetPacketType(&packetType);
                const psnr::core::NrStatus payloadStatus = event.GetPayload(&payloadView);
                if (packetTypeStatus.Failed() || payloadStatus.Failed() || payloadView.data == nullptr)
                {
                    return "World client received invalid packet metadata";
                }

                *outReceived = false;
                if (packetType.value != static_cast<std::uint16_t>(psnr::world::protocol::S2CPacketType::RoundResult))
                {
                    return {};
                }

                const std::span<const std::byte> payload{payloadView.data, payloadView.size};
                if (psnr::world::protocol::v2::RoundResult::Decode(payload, outResult) !=
                    psnr::world::protocol::WorldProtocolError::Success)
                {
                    return "World client failed to decode RoundResult";
                }
                *outReceived = true;
                return {};
            }

            [[nodiscard]] static std::string TryDecodeControlledEntityRebind(
                const psnr::runtime::NrClientEvent& event,
                psnr::world::protocol::v1::ControlledEntityRebind* const outRebind, bool* const outReceived)
            {
                psnr::core::NrPacketType packetType;
                psnr::runtime::NrByteView payloadView;
                const psnr::core::NrStatus packetTypeStatus = event.GetPacketType(&packetType);
                const psnr::core::NrStatus payloadStatus = event.GetPayload(&payloadView);
                if (packetTypeStatus.Failed() || payloadStatus.Failed() || payloadView.data == nullptr)
                {
                    return "World client received invalid packet metadata";
                }

                *outReceived = false;
                if (packetType.value !=
                    static_cast<std::uint16_t>(psnr::world::protocol::S2CPacketType::ControlledEntityRebind))
                {
                    return {};
                }

                const std::span<const std::byte> payload{payloadView.data, payloadView.size};
                if (psnr::world::protocol::v1::ControlledEntityRebind::Decode(payload, outRebind) !=
                    psnr::world::protocol::WorldProtocolError::Success)
                {
                    return "World client failed to decode ControlledEntityRebind";
                }
                *outReceived = true;
                return {};
            }
        };
    } // namespace

    std::string BenchmarkWorldClient::Start(const psnr::runtime::NrEndpoint& endpoint,
                                            const psnr::runtime::NrClientConfig& clientConfig,
                                            const std::uint32_t joinTimeoutMilliseconds)
    {
        if (client_.IsValid() || joinTimeoutMilliseconds == 0)
        {
            return "World client start state is invalid";
        }

        playerId_ = 0;
        controlledEntityId_ = 0;
        controlledEntityGeneration_ = 0;
        nextControlSequence_ = 1;
        roundResult_ = psnr::world::protocol::v2::RoundResult{};
        joined_ = false;
        roundResultCommitted_ = false;
        resultConnectionClosed_ = false;

        psnr::runtime::NrClient client;
        const psnr::core::NrStatus createStatus = psnr::runtime::NrClient::Create(clientConfig, &client);
        if (createStatus.Failed())
        {
            return BenchmarkClientTransport::DescribeFailure("NrClient::Create", createStatus);
        }

        const psnr::core::NrStatus connectStatus = client.Connect(endpoint);
        if (connectStatus.Failed())
        {
            return BenchmarkClientTransport::DescribeFailure("NrClient::Connect", connectStatus);
        }

        const std::chrono::steady_clock::time_point deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds{joinTimeoutMilliseconds};
        bool joinSent = false;
        bool readyReceived = false;
        std::uint64_t packetEventCount = 0;
        std::vector<psnr::world::protocol::v2::EntitySpawn> spawns;
        psnr::world::protocol::v2::WorldReady ready;
        while (std::chrono::steady_clock::now() < deadline)
        {
            psnr::runtime::NrClientEvent event;
            const std::string eventError = BenchmarkClientTransport::ReadNextEventUntil(client, deadline, &event);
            if (!eventError.empty())
            {
                psnr::runtime::NrClientSnapshot snapshot;
                const psnr::core::NrStatus snapshotStatus = client.CaptureSnapshot(&snapshot);
                std::string detailedError = eventError;
                detailedError.append(": packetEventCount=");
                detailedError.append(std::to_string(packetEventCount));
                detailedError.append(" spawnCount=");
                detailedError.append(std::to_string(spawns.size()));
                if (snapshotStatus.Succeeded())
                {
                    detailedError.append(" lifecycle=");
                    detailedError.append(std::to_string(static_cast<int>(snapshot.LifecycleState())));
                    detailedError.append(" eventQueueDepth=");
                    detailedError.append(std::to_string(snapshot.EventQueueDepth()));
                    detailedError.append(" eventQueueHighWatermark=");
                    detailedError.append(std::to_string(snapshot.EventQueueHighWatermark()));
                }
                return detailedError;
            }

            if (event.Kind() == psnr::runtime::NrClientEventKind::TransportConnected)
            {
                if (joinSent)
                {
                    return "World client received duplicate TransportConnected";
                }
                std::array<std::byte, psnr::world::protocol::v2::JoinWorldRequest::Wire::MinimumPayloadBytes> payload;
                if (psnr::world::protocol::v2::JoinWorldRequest::Encode(psnr::world::protocol::v2::JoinWorldRequest{},
                                                                        payload) !=
                    psnr::world::protocol::WorldProtocolError::Success)
                {
                    return "World client failed to encode JoinWorldRequest";
                }
                const psnr::runtime::NrByteView payloadView{payload.data(), static_cast<std::uint32_t>(payload.size())};
                const psnr::core::NrStatus sendStatus =
                    client.Send(psnr::core::NrPacketType{static_cast<std::uint16_t>(
                                    psnr::world::protocol::C2SPacketType::JoinWorldRequest)},
                                payloadView);
                if (sendStatus.Failed())
                {
                    return BenchmarkClientTransport::DescribeFailure("NrClient::Send(JoinWorldRequest)", sendStatus);
                }
                joinSent = true;
            }
            else if (event.Kind() == psnr::runtime::NrClientEventKind::PacketReceived)
            {
                ++packetEventCount;
                if (!joinSent)
                {
                    return "World client received gameplay packet before join submission";
                }
                const std::string decodeError =
                    BenchmarkWorldClientHelper::DecodePacket(event, &spawns, &ready, &readyReceived);
                if (!decodeError.empty())
                {
                    return decodeError;
                }
                if (readyReceived)
                {
                    if (!BenchmarkWorldClientHelper::ContainsControlledEntity(spawns, ready))
                    {
                        return "World client join baseline is inconsistent";
                    }
                    client_ = std::move(client);
                    playerId_ = ready.playerId;
                    controlledEntityId_ = ready.controlledEntityId;
                    controlledEntityGeneration_ = ready.controlledEntityGeneration;
                    joined_ = true;
                    return {};
                }
            }
            else if (event.Kind() == psnr::runtime::NrClientEventKind::TransportConnectionFailed ||
                     event.Kind() == psnr::runtime::NrClientEventKind::TransportDisconnected)
            {
                psnr::core::NrStatus transportStatus;
                static_cast<void>(event.GetTransportStatus(&transportStatus));
                std::string transportError = "World client transport ended before join completed: kind=";
                transportError.append(std::to_string(static_cast<int>(event.Kind())));
                transportError.append(" errorCode=");
                transportError.append(std::to_string(static_cast<int>(transportStatus.ErrorCode())));
                transportError.append(" nativeErrorCode=");
                transportError.append(std::to_string(transportStatus.NativeErrorCode()));
                if (event.Kind() == psnr::runtime::NrClientEventKind::TransportDisconnected)
                {
                    psnr::runtime::NrClientDisconnectReason disconnectReason =
                        psnr::runtime::NrClientDisconnectReason::None;
                    static_cast<void>(event.GetDisconnectReason(&disconnectReason));
                    transportError.append(" reason=");
                    transportError.append(std::to_string(static_cast<int>(disconnectReason)));
                }
                return transportError;
            }
        }

        psnr::runtime::NrClientSnapshot snapshot;
        const psnr::core::NrStatus snapshotStatus = client.CaptureSnapshot(&snapshot);
        std::string timeoutError = "World client join timed out: spawnCount=";
        timeoutError.append(std::to_string(spawns.size()));
        if (snapshotStatus.Succeeded())
        {
            timeoutError.append(" lifecycle=");
            timeoutError.append(std::to_string(static_cast<int>(snapshot.LifecycleState())));
            timeoutError.append(" eventQueueDepth=");
            timeoutError.append(std::to_string(snapshot.EventQueueDepth()));
            timeoutError.append(" eventQueueHighWatermark=");
            timeoutError.append(std::to_string(snapshot.EventQueueHighWatermark()));
        }
        return timeoutError;
    }

    std::string BenchmarkWorldClient::Shutdown() noexcept
    {
        if (!client_.IsValid())
        {
            return {};
        }
        const psnr::core::NrStatus shutdownStatus = client_.Shutdown();
        if (shutdownStatus.Failed())
        {
            return BenchmarkClientTransport::DescribeFailure("NrClient::Shutdown", shutdownStatus);
        }
        client_ = psnr::runtime::NrClient{};
        joined_ = false;
        return {};
    }

    std::string BenchmarkWorldClient::DrainAvailableEvents()
    {
        if (resultConnectionClosed_)
        {
            return {};
        }
        if (!client_.IsValid() || !joined_)
        {
            return "World client drain state is invalid";
        }

        while (true)
        {
            psnr::runtime::NrClientEvent event;
            bool eventRead = false;
            const std::string readError = BenchmarkClientTransport::TryReadNextEvent(client_, &event, &eventRead);
            if (!readError.empty())
            {
                return readError;
            }
            if (!eventRead)
            {
                return {};
            }
            if (event.Kind() == psnr::runtime::NrClientEventKind::PacketReceived)
            {
                psnr::world::protocol::v1::ControlledEntityRebind rebind;
                bool rebindReceived = false;
                const std::string rebindError =
                    BenchmarkWorldClientHelper::TryDecodeControlledEntityRebind(event, &rebind, &rebindReceived);
                if (!rebindError.empty())
                {
                    return rebindError;
                }
                if (rebindReceived)
                {
                    if (rebind.playerId != playerId_ || rebind.previousEntityId != controlledEntityId_ ||
                        rebind.previousEntityGeneration != controlledEntityGeneration_)
                    {
                        return "World client received inconsistent ControlledEntityRebind";
                    }
                    controlledEntityId_ = rebind.controlledEntityId;
                    controlledEntityGeneration_ = rebind.controlledEntityGeneration;
                    nextControlSequence_ = 1;
                    continue;
                }

                psnr::world::protocol::v2::RoundResult roundResult;
                bool roundResultReceived = false;
                const std::string decodeError =
                    BenchmarkWorldClientHelper::TryDecodeRoundResult(event, &roundResult, &roundResultReceived);
                if (!decodeError.empty())
                {
                    return decodeError;
                }
                if (!roundResultReceived)
                {
                    continue;
                }
                if (roundResultCommitted_)
                {
                    return "World client received duplicate RoundResult";
                }

                roundResult_ = std::move(roundResult);
                roundResultCommitted_ = true;
                const psnr::core::NrStatus shutdownStatus = client_.Shutdown();
                if (shutdownStatus.Failed())
                {
                    return BenchmarkClientTransport::DescribeFailure("NrClient::Shutdown(after RoundResult)",
                                                                     shutdownStatus);
                }
                client_ = psnr::runtime::NrClient{};
                joined_ = false;
                resultConnectionClosed_ = true;
                return {};
            }
            if (event.Kind() == psnr::runtime::NrClientEventKind::TransportDisconnected)
            {
                psnr::runtime::NrClientDisconnectReason disconnectReason =
                    psnr::runtime::NrClientDisconnectReason::None;
                psnr::core::NrStatus transportStatus;
                static_cast<void>(event.GetDisconnectReason(&disconnectReason));
                static_cast<void>(event.GetTransportStatus(&transportStatus));

                std::string disconnectError = "World client disconnected during admission: reason=";
                disconnectError.append(std::to_string(static_cast<int>(disconnectReason)));
                disconnectError.append(" errorCode=");
                disconnectError.append(std::to_string(static_cast<int>(transportStatus.ErrorCode())));
                disconnectError.append(" nativeErrorCode=");
                disconnectError.append(std::to_string(transportStatus.NativeErrorCode()));
                return disconnectError;
            }
            return "World client received an unexpected lifecycle event during admission";
        }
    }

    std::string BenchmarkWorldClient::SendControlState(const psnr::world::protocol::v2::TurnState turnState,
                                                       const psnr::world::protocol::v2::BoostState boostState)
    {
        if (!client_.IsValid() || !joined_ || roundResultCommitted_)
        {
            return "World client control send state is invalid";
        }
        if (nextControlSequence_ == 0 || nextControlSequence_ == (std::numeric_limits<std::uint32_t>::max)())
        {
            return "World client control sequence is exhausted";
        }

        const psnr::world::protocol::v2::ControlStateCommand command{controlledEntityGeneration_, nextControlSequence_,
                                                                     turnState, boostState};
        std::array<std::byte, psnr::world::protocol::v2::ControlStateCommand::Wire::PayloadBytes> payload{};
        if (psnr::world::protocol::v2::ControlStateCommand::Encode(command, payload) !=
            psnr::world::protocol::WorldProtocolError::Success)
        {
            return "World client failed to encode ControlStateCommand";
        }

        const psnr::runtime::NrByteView payloadView{payload.data(), static_cast<std::uint32_t>(payload.size())};
        const psnr::core::NrStatus sendStatus =
            client_.Send(psnr::core::NrPacketType{static_cast<std::uint16_t>(
                             psnr::world::protocol::C2SPacketType::ControlStateCommand)},
                         payloadView);
        if (sendStatus.Failed())
        {
            return BenchmarkClientTransport::DescribeFailure("NrClient::Send(ControlStateCommand)", sendStatus);
        }
        ++nextControlSequence_;
        return {};
    }

    bool BenchmarkWorldClient::Joined() const noexcept
    {
        return joined_;
    }

    std::uint32_t BenchmarkWorldClient::PlayerId() const noexcept
    {
        return playerId_;
    }

    std::uint32_t BenchmarkWorldClient::ControlledEntityId() const noexcept
    {
        return controlledEntityId_;
    }

    std::uint32_t BenchmarkWorldClient::ControlledEntityGeneration() const noexcept
    {
        return controlledEntityGeneration_;
    }

    bool BenchmarkWorldClient::HasRoundResult() const noexcept
    {
        return roundResultCommitted_;
    }

    const psnr::world::protocol::v2::RoundResult& BenchmarkWorldClient::CommittedRoundResult() const noexcept
    {
        return roundResult_;
    }
} // namespace psnr::benchmark
