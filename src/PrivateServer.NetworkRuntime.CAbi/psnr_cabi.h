#pragma once

#include <stdint.h>

#if defined(PSNR_CABI_EXPORTS)
#define PSNR_CABI __declspec(dllexport)
#else
#define PSNR_CABI __declspec(dllimport)
#endif

#if defined(_MSC_VER) // calling convention for MSVC compatible compiler
#define PSNR_CABI_CALL __cdecl
#else
#define PSNR_CABI_CALL
#endif

#if defined(__cplusplus)
// only for C++
extern "C"
{
#endif

    typedef struct psnr_client psnr_client; // client handle
    typedef struct psnr_client_event psnr_client_event;

    typedef struct psnr_status
    {
        uint32_t error_code;
        uint32_t native_error_code;
    } psnr_status;

    typedef struct psnr_client_config
    {
        uint32_t event_queue_capacity;
        uint32_t payload_queue_capacity;
    } psnr_client_config;

    typedef struct psnr_ipv4_endpoint
    {
        uint8_t address[4];
        uint16_t port;
        uint16_t reserved;
    } psnr_ipv4_endpoint;

    typedef struct psnr_byte_view
    {
        const uint8_t* data;
        uint32_t size;
    } psnr_byte_view;

    typedef struct psnr_client_snapshot
    {
        uint32_t lifecycle_state;
        uint32_t reserved;
        uint64_t pending_connect_io_count;
        uint64_t pending_recv_io_count;
        uint64_t pending_send_io_count;
        uint64_t pending_io_count;
        uint64_t event_queue_depth;
        uint64_t event_queue_high_watermark;
        uint64_t pending_send_queue_depth;
        uint64_t pending_send_queue_high_watermark;
    } psnr_client_snapshot;

    enum // ERROR_CODE
    {
        PSNR_ERROR_SUCCESS = 0,
        PSNR_ERROR_INVALID_ARGUMENT = 1,
        PSNR_ERROR_INVALID_STATE = 2,
        PSNR_ERROR_OUT_OF_MEMORY = 3,
        PSNR_ERROR_CAPACITY_EXCEEDED = 4,
        PSNR_ERROR_QUEUE_FULL = 5,
        PSNR_ERROR_QUEUE_EMPTY = 6,
        PSNR_ERROR_IO_FAILED = 7,
        PSNR_ERROR_OPERATION_CANCELED = 8,
        PSNR_ERROR_PROTOCOL_ERROR = 9,
        PSNR_ERROR_UNKNOWN = UINT32_MAX,
    };

    enum // CLIENT_EVENT_TYPE
    {
        PSNR_CLIENT_EVENT_NONE = 0,
        PSNR_CLIENT_EVENT_TRANSPORT_CONNECTED = 1,
        PSNR_CLIENT_EVENT_TRANSPORT_CONNECTION_FAILED = 2,
        PSNR_CLIENT_EVENT_PACKET_RECEIVED = 3,
        PSNR_CLIENT_EVENT_TRANSPORT_DISCONNECTED = 4,
        PSNR_CLIENT_EVENT_UNKNOWN = UINT32_MAX,
    };

    enum // CLIENT_DISCONNECT_REASON
    {
        PSNR_CLIENT_DISCONNECT_NONE = 0,
        PSNR_CLIENT_DISCONNECT_LOCAL_REQUESTED = 1,
        PSNR_CLIENT_DISCONNECT_REMOTE_CLOSED = 2,
        PSNR_CLIENT_DISCONNECT_RECEIVE_PRESSURE = 3,
        PSNR_CLIENT_DISCONNECT_TRANSPORT_ERROR = 4,
        PSNR_CLIENT_DISCONNECT_PROTOCOL_ERROR = 5,
        PSNR_CLIENT_DISCONNECT_UNKNOWN = UINT32_MAX,
    };

    enum // CLIENT_LIFECYCLE_STATE
    {
        PSNR_CLIENT_LIFECYCLE_INVALID = 0,
        PSNR_CLIENT_LIFECYCLE_IDLE = 1,
        PSNR_CLIENT_LIFECYCLE_TRANSPORT_CONNECTING = 2,
        PSNR_CLIENT_LIFECYCLE_TRANSPORT_CONNECTED = 3,
        PSNR_CLIENT_LIFECYCLE_TRANSPORT_DISCONNECTING = 4,
        PSNR_CLIENT_LIFECYCLE_SHUTDOWN = 5,
        PSNR_CLIENT_LIFECYCLE_UNKNOWN = UINT32_MAX,
    };

    PSNR_CABI psnr_client_config PSNR_CABI_CALL psnr_client_config_default(void);

    PSNR_CABI psnr_status PSNR_CABI_CALL psnr_client_create(const psnr_client_config* config, psnr_client** out_client);

    PSNR_CABI void PSNR_CABI_CALL psnr_client_destroy(psnr_client* client);

    PSNR_CABI psnr_status PSNR_CABI_CALL psnr_client_connect_ipv4(psnr_client* client,
                                                                  const psnr_ipv4_endpoint* endpoint);

    PSNR_CABI psnr_status PSNR_CABI_CALL psnr_client_disconnect(psnr_client* client);

    PSNR_CABI psnr_status PSNR_CABI_CALL psnr_client_shutdown(psnr_client* client);

    PSNR_CABI psnr_status PSNR_CABI_CALL psnr_client_send(psnr_client* client, uint32_t packet_type,
                                                          const uint8_t* payload, uint32_t payload_size);

    PSNR_CABI psnr_status PSNR_CABI_CALL psnr_client_try_pop_event(psnr_client* client, psnr_client_event** out_event);

    PSNR_CABI psnr_status PSNR_CABI_CALL psnr_client_capture_snapshot(const psnr_client* client,
                                                                      psnr_client_snapshot* out_snapshot);

    PSNR_CABI void PSNR_CABI_CALL psnr_client_event_destroy(psnr_client_event* event);

    PSNR_CABI psnr_status PSNR_CABI_CALL psnr_client_event_get_kind(const psnr_client_event* event, uint32_t* out_kind);

    PSNR_CABI psnr_status PSNR_CABI_CALL psnr_client_event_get_packet_type(const psnr_client_event* event,
                                                                           uint32_t* out_packet_type);

    PSNR_CABI psnr_status PSNR_CABI_CALL psnr_client_event_get_payload(const psnr_client_event* event,
                                                                       psnr_byte_view* out_payload);

    PSNR_CABI psnr_status PSNR_CABI_CALL psnr_client_event_get_transport_status(const psnr_client_event* event,
                                                                                psnr_status* out_status);

    PSNR_CABI psnr_status PSNR_CABI_CALL psnr_client_event_get_disconnect_reason(const psnr_client_event* event,
                                                                                 uint32_t* out_reason);

#if defined(__cplusplus)
}
#endif
