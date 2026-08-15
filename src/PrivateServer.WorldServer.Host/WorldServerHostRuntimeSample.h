#pragma once

#include <PrivateServer/NetworkRuntime/NrServerSnapshot.h>
#include <PrivateServer/NetworkRuntime/NrStatus.h>

#include <cstdint>

namespace psnr::world::host
{
    // Host control thread가 Runtime 상태를 이 값에 복사한다. artifact queue에 들어간 뒤에는
    // 직렬화가 끝날 때까지 writer thread만 소유하며, writer가 Runtime 객체에 직접 접근하지 않는다.
    struct WorldServerHostRuntimeSample final
    {
        std::uint64_t sequence = 0;
        std::uint64_t elapsedMilliseconds = 0;
        std::uint64_t captureDurationNanoseconds = 0;
        psnr::core::NrStatus captureStatus{};
        psnr::runtime::NrServerSnapshot snapshot{};
    };
} // namespace psnr::world::host
