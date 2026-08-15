#pragma once

#include <PrivateServer/NetworkRuntime/NrServer.h>
#include <PrivateServer/NetworkRuntime/NrToWorldEvent.h>

namespace psnr::world
{
    // NetworkRuntime public server의 ToWorld queue를 읽는 integration adapter다.
    // World protocol/domain/simulation에는 이 Runtime type을 전달하지 않는다.
    class NrServerWorldEventSource final
    {
    public:
        explicit NrServerWorldEventSource(psnr::runtime::NrServer& server) noexcept
            : server_(server)
        {
        }

        [[nodiscard]] psnr::core::NrStatus TryPopBatch(psnr::runtime::NrToWorldEvent* const eventBuffer,
                                                       const std::size_t eventBufferCount,
                                                       std::size_t* const outEventCount) noexcept
        {
            return server_.TryPopToWorldEvents(eventBuffer, eventBufferCount, outEventCount);
        }

        [[nodiscard]] psnr::core::NrStatus WaitForEvents(
            const std::uint32_t timeoutMilliseconds,
            psnr::runtime::NrToWorldWaitResult* const outWaitResult) noexcept
        {
            return server_.WaitForToWorldEvents(timeoutMilliseconds, outWaitResult);
        }

    private:
        psnr::runtime::NrServer& server_;
    };
} // namespace psnr::world
