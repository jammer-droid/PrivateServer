#include "psnr_cabi.h"

#include <PrivateServer/NetworkRuntime/NrClient.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <utility>

static_assert(sizeof(psnr_ipv4_endpoint) == 8);
static_assert(offsetof(psnr_ipv4_endpoint, port) == 4);
static_assert(offsetof(psnr_ipv4_endpoint, reserved) == 6);
static_assert(offsetof(psnr_byte_view, size) == sizeof(void*));
static_assert(sizeof(psnr_client_snapshot) == 72);
static_assert(offsetof(psnr_client_snapshot, pending_connect_io_count) == 8);

namespace
{
    using psnr::core::NrErrorCode;
    using psnr::core::NrStatus;
    using psnr::runtime::NrClient;
    using psnr::runtime::NrClientConfig;
    using psnr::runtime::NrClientDisconnectReason;
    using psnr::runtime::NrClientEvent;
    using psnr::runtime::NrClientEventKind;
    using psnr::runtime::NrClientLifecycleState;
    using psnr::runtime::NrClientSnapshot;
    using psnr::runtime::NrEndpoint;
    using psnr::runtime::NrEndpointAddressType;
    using psnr::runtime::NrIPv4Address;

    [[nodiscard]] std::uint32_t ToCAbiEventKind(const NrClientEventKind kind) noexcept
    {
        switch (kind)
        {
        case NrClientEventKind::None:
            return PSNR_CLIENT_EVENT_NONE;
        case NrClientEventKind::TransportConnected:
            return PSNR_CLIENT_EVENT_TRANSPORT_CONNECTED;
        case NrClientEventKind::TransportConnectionFailed:
            return PSNR_CLIENT_EVENT_TRANSPORT_CONNECTION_FAILED;
        case NrClientEventKind::PacketReceived:
            return PSNR_CLIENT_EVENT_PACKET_RECEIVED;
        case NrClientEventKind::TransportDisconnected:
            return PSNR_CLIENT_EVENT_TRANSPORT_DISCONNECTED;
        }

        return static_cast<std::uint32_t>(PSNR_CLIENT_EVENT_UNKNOWN);
    }

    [[nodiscard]] std::uint32_t ToCAbiDisconnectReason(const NrClientDisconnectReason reason) noexcept
    {
        switch (reason)
        {
        case NrClientDisconnectReason::None:
            return PSNR_CLIENT_DISCONNECT_NONE;
        case NrClientDisconnectReason::LocalRequested:
            return PSNR_CLIENT_DISCONNECT_LOCAL_REQUESTED;
        case NrClientDisconnectReason::RemoteClosed:
            return PSNR_CLIENT_DISCONNECT_REMOTE_CLOSED;
        case NrClientDisconnectReason::ReceivePressure:
            return PSNR_CLIENT_DISCONNECT_RECEIVE_PRESSURE;
        case NrClientDisconnectReason::TransportError:
            return PSNR_CLIENT_DISCONNECT_TRANSPORT_ERROR;
        case NrClientDisconnectReason::ProtocolError:
            return PSNR_CLIENT_DISCONNECT_PROTOCOL_ERROR;
        }

        return static_cast<std::uint32_t>(PSNR_CLIENT_DISCONNECT_UNKNOWN);
    }

    [[nodiscard]] std::uint32_t ToCAbiLifecycleState(const NrClientLifecycleState state) noexcept
    {
        switch (state)
        {
        case NrClientLifecycleState::Invalid:
            return PSNR_CLIENT_LIFECYCLE_INVALID;
        case NrClientLifecycleState::Idle:
            return PSNR_CLIENT_LIFECYCLE_IDLE;
        case NrClientLifecycleState::TransportConnecting:
            return PSNR_CLIENT_LIFECYCLE_TRANSPORT_CONNECTING;
        case NrClientLifecycleState::TransportConnected:
            return PSNR_CLIENT_LIFECYCLE_TRANSPORT_CONNECTED;
        case NrClientLifecycleState::TransportDisconnecting:
            return PSNR_CLIENT_LIFECYCLE_TRANSPORT_DISCONNECTING;
        case NrClientLifecycleState::Shutdown:
            return PSNR_CLIENT_LIFECYCLE_SHUTDOWN;
        }

        return static_cast<std::uint32_t>(PSNR_CLIENT_LIFECYCLE_UNKNOWN);
    }

    [[nodiscard]] std::uint32_t ToCAbiErrorCode(const NrErrorCode errorCode) noexcept
    {
        switch (errorCode)
        {
        case NrErrorCode::Success:
            return PSNR_ERROR_SUCCESS;
        case NrErrorCode::InvalidArgument:
            return PSNR_ERROR_INVALID_ARGUMENT;
        case NrErrorCode::InvalidState:
            return PSNR_ERROR_INVALID_STATE;
        case NrErrorCode::OutOfMemory:
        case NrErrorCode::PoolExhausted:
            return PSNR_ERROR_OUT_OF_MEMORY;
        case NrErrorCode::CapacityExceeded:
            return PSNR_ERROR_CAPACITY_EXCEEDED;
        case NrErrorCode::QueueFull:
            return PSNR_ERROR_QUEUE_FULL;
        case NrErrorCode::QueueEmpty:
            return PSNR_ERROR_QUEUE_EMPTY;
        case NrErrorCode::IoFailed:
            return PSNR_ERROR_IO_FAILED;
        case NrErrorCode::OperationCanceled:
            return PSNR_ERROR_OPERATION_CANCELED;
        case NrErrorCode::ProtocolError:
            return PSNR_ERROR_PROTOCOL_ERROR;
        case NrErrorCode::DispatchRuleNotFound:
            return static_cast<std::uint32_t>(PSNR_ERROR_UNKNOWN);
        }

        return static_cast<std::uint32_t>(PSNR_ERROR_UNKNOWN);
    }

    [[nodiscard]] psnr_status ToCAbiStatus(const NrStatus status) noexcept
    {
        return psnr_status{ToCAbiErrorCode(status.ErrorCode()), status.NativeErrorCode()};
    }

    [[nodiscard]] psnr_status InvalidArgumentStatus() noexcept
    {
        return psnr_status{PSNR_ERROR_INVALID_ARGUMENT, 0};
    }
} // namespace

struct psnr_client final
{
    explicit psnr_client(NrClient&& clientValue) noexcept
        : client(std::move(clientValue))
    {
    }

    NrClient client;
};

struct psnr_client_event final
{
    explicit psnr_client_event(NrClientEvent&& eventValue) noexcept
        : event(std::move(eventValue))
    {
    }

    NrClientEvent event;
};

psnr_client_config PSNR_CABI_CALL psnr_client_config_default(void)
{
    return psnr_client_config{
        static_cast<std::uint32_t>(psnr::runtime::NrDefaultClientEventQueueCapacity),
        static_cast<std::uint32_t>(psnr::runtime::NrDefaultClientPayloadQueueCapacity),
    };
}

psnr_status PSNR_CABI_CALL psnr_client_create(const psnr_client_config* config, psnr_client** out_client)
{
    if (config == nullptr || out_client == nullptr)
    {
        return InvalidArgumentStatus();
    }

    NrClientConfig nativeConfig;
    nativeConfig.eventQueueCapacity = config->event_queue_capacity;
    nativeConfig.payloadQueueCapacity = config->payload_queue_capacity;

    NrClient nativeClient;
    const NrStatus createStatus = NrClient::Create(nativeConfig, &nativeClient);
    if (createStatus.Failed())
    {
        return ToCAbiStatus(createStatus);
    }

    psnr_client* client = new (std::nothrow) psnr_client(std::move(nativeClient));
    if (client == nullptr)
    {
        return psnr_status{PSNR_ERROR_OUT_OF_MEMORY, 0};
    }

    *out_client = client;
    return psnr_status{PSNR_ERROR_SUCCESS, 0};
}

void PSNR_CABI_CALL psnr_client_destroy(psnr_client* client)
{
    delete client;
}

psnr_status PSNR_CABI_CALL psnr_client_connect_ipv4(psnr_client* client, const psnr_ipv4_endpoint* endpoint)
{
    if (client == nullptr || endpoint == nullptr || endpoint->reserved != 0)
    {
        return InvalidArgumentStatus();
    }

    const NrEndpoint nativeEndpoint{
        NrEndpointAddressType::IPv4,
        NrIPv4Address{
            endpoint->address[0],
            endpoint->address[1],
            endpoint->address[2],
            endpoint->address[3],
        },
        endpoint->port,
    };
    return ToCAbiStatus(client->client.Connect(nativeEndpoint));
}

psnr_status PSNR_CABI_CALL psnr_client_disconnect(psnr_client* client)
{
    return client == nullptr ? InvalidArgumentStatus() : ToCAbiStatus(client->client.Disconnect());
}

psnr_status PSNR_CABI_CALL psnr_client_shutdown(psnr_client* client)
{
    return client == nullptr ? InvalidArgumentStatus() : ToCAbiStatus(client->client.Shutdown());
}

psnr_status PSNR_CABI_CALL psnr_client_send(psnr_client* client, const std::uint32_t packet_type,
                                            const std::uint8_t* payload, const std::uint32_t payload_size)
{
    if (client == nullptr || (payload == nullptr && payload_size != 0) ||
        packet_type > std::numeric_limits<std::uint16_t>::max())
    {
        return InvalidArgumentStatus();
    }

    const psnr::core::NrPacketType nativePacketType{static_cast<std::uint16_t>(packet_type)};
    const psnr::runtime::NrByteView nativePayload{reinterpret_cast<const std::byte*>(payload), payload_size};

    return ToCAbiStatus(client->client.Send(nativePacketType, nativePayload));
}

psnr_status PSNR_CABI_CALL psnr_client_try_pop_event(psnr_client* client, psnr_client_event** out_event)
{
    if (out_event == nullptr)
    {
        return InvalidArgumentStatus();
    }

    *out_event = nullptr;
    if (client == nullptr)
    {
        return InvalidArgumentStatus();
    }

    NrClientEvent nativeEvent;
    const NrStatus popStatus = client->client.TryPopEvent(&nativeEvent);
    if (popStatus.Failed())
    {
        return ToCAbiStatus(popStatus);
    }

    psnr_client_event* event = new (std::nothrow) psnr_client_event(std::move(nativeEvent));
    if (event == nullptr)
    {
        return psnr_status{PSNR_ERROR_OUT_OF_MEMORY, 0};
    }

    *out_event = event;
    return psnr_status{PSNR_ERROR_SUCCESS, 0};
}

psnr_status PSNR_CABI_CALL psnr_client_capture_snapshot(const psnr_client* client, psnr_client_snapshot* out_snapshot)
{
    if (client == nullptr || out_snapshot == nullptr)
    {
        return InvalidArgumentStatus();
    }

    NrClientSnapshot nativeSnapshot;
    const NrStatus captureStatus = client->client.CaptureSnapshot(&nativeSnapshot);
    if (captureStatus.Failed())
    {
        return ToCAbiStatus(captureStatus);
    }

    const psnr_client_snapshot snapshot{
        ToCAbiLifecycleState(nativeSnapshot.LifecycleState()),
        0,
        nativeSnapshot.PendingConnectIoCount(),
        nativeSnapshot.PendingRecvIoCount(),
        nativeSnapshot.PendingSendIoCount(),
        nativeSnapshot.PendingIoCount(),
        nativeSnapshot.EventQueueDepth(),
        nativeSnapshot.EventQueueHighWatermark(),
        nativeSnapshot.PendingSendQueueDepth(),
        nativeSnapshot.PendingSendQueueHighWatermark(),
    };
    *out_snapshot = snapshot;
    return psnr_status{PSNR_ERROR_SUCCESS, 0};
}

void PSNR_CABI_CALL psnr_client_event_destroy(psnr_client_event* event)
{
    delete event;
}

psnr_status PSNR_CABI_CALL psnr_client_event_get_kind(const psnr_client_event* event, std::uint32_t* out_kind)
{
    if (event == nullptr || out_kind == nullptr)
    {
        return InvalidArgumentStatus();
    }

    const std::uint32_t kind = ToCAbiEventKind(event->event.Kind());
    *out_kind = kind;
    return psnr_status{PSNR_ERROR_SUCCESS, 0};
}

psnr_status PSNR_CABI_CALL psnr_client_event_get_packet_type(const psnr_client_event* event,
                                                             std::uint32_t* out_packet_type)
{
    if (event == nullptr || out_packet_type == nullptr)
    {
        return InvalidArgumentStatus();
    }

    psnr::core::NrPacketType nativePacketType;
    const NrStatus accessorStatus = event->event.GetPacketType(&nativePacketType);
    if (accessorStatus.Failed())
    {
        return ToCAbiStatus(accessorStatus);
    }

    *out_packet_type = nativePacketType.value;
    return psnr_status{PSNR_ERROR_SUCCESS, 0};
}

psnr_status PSNR_CABI_CALL psnr_client_event_get_payload(const psnr_client_event* event, psnr_byte_view* out_payload)
{
    if (event == nullptr || out_payload == nullptr)
    {
        return InvalidArgumentStatus();
    }

    psnr::runtime::NrByteView nativePayload;
    const NrStatus accessorStatus = event->event.GetPayload(&nativePayload);
    if (accessorStatus.Failed())
    {
        return ToCAbiStatus(accessorStatus);
    }

    const psnr_byte_view payload{reinterpret_cast<const std::uint8_t*>(nativePayload.data), nativePayload.size};
    *out_payload = payload;
    return psnr_status{PSNR_ERROR_SUCCESS, 0};
}

psnr_status PSNR_CABI_CALL psnr_client_event_get_transport_status(const psnr_client_event* event,
                                                                  psnr_status* out_status)
{
    if (event == nullptr || out_status == nullptr)
    {
        return InvalidArgumentStatus();
    }

    NrStatus nativeTransportStatus;
    const NrStatus accessorStatus = event->event.GetTransportStatus(&nativeTransportStatus);
    if (accessorStatus.Failed())
    {
        return ToCAbiStatus(accessorStatus);
    }

    *out_status = ToCAbiStatus(nativeTransportStatus);
    return psnr_status{PSNR_ERROR_SUCCESS, 0};
}

psnr_status PSNR_CABI_CALL psnr_client_event_get_disconnect_reason(const psnr_client_event* event,
                                                                   std::uint32_t* out_reason)
{
    if (event == nullptr || out_reason == nullptr)
    {
        return InvalidArgumentStatus();
    }

    NrClientDisconnectReason nativeReason;
    const NrStatus accessorStatus = event->event.GetDisconnectReason(&nativeReason);
    if (accessorStatus.Failed())
    {
        return ToCAbiStatus(accessorStatus);
    }

    *out_reason = ToCAbiDisconnectReason(nativeReason);
    return psnr_status{PSNR_ERROR_SUCCESS, 0};
}
