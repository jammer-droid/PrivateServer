#pragma once

#include <cstdint>

namespace psnr::runtime
{
    enum class NrSessionEndReason : std::uint8_t
    {
        None,                 // 아직 종료 사유가 기록되지 않은 상태. SessionClosed event에는 사용할 수 없다.
        ApplicationRequested, // World/application이 정상적인 세션 종료를 요청했다.
        ApplicationPolicy,    // 인증, 중복 접속, 제재 등 application 정책에 따라 종료했다.
        ProtocolError,        // 잘못된 frame, packet type 등 wire protocol 위반을 감지했다.
        RemoteClosed,         // 상대가 정상적인 0-byte recv completion으로 연결을 닫았다.
        ReceivePressure,      // recv buffer, input pool, ingress queue의 수용 한계를 초과했다.
        SendPressure,         // session send backlog의 수용 한계를 초과했다.
        TransportError,       // IO 실패 또는 Runtime transport 불변식 위반을 감지했다.
        ServerStopping,       // server stop/shutdown 절차가 세션 종료를 시작했다.
    };
} // namespace psnr::runtime
