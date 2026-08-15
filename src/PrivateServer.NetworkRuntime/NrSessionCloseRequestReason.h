#pragma once

#include <cstdint>

namespace psnr::runtime
{
    enum class NrSessionCloseRequestReason : std::uint8_t
    {
        None,                 // 유효한 close 요청이 없는 상태. API 입력으로 사용할 수 없다.
        ApplicationRequested, // 정상적인 application 흐름에서 세션 종료를 요청한다.
        ApplicationPolicy,    // 인증, 중복 접속, 제재 등 application 정책으로 종료를 요청한다.
        ProtocolError,        // World payload decode/validation에서 protocol 위반을 확인했다.
    };
} // namespace psnr::runtime
