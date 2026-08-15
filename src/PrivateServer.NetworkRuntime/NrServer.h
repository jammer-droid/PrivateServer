#pragma once

#include "Export.h"
#include "NrServerConfig.h"
#include "NrSessionCloseRequestReason.h"
#include "NrSessionKey.h"
#include "NrStatus.h"
#include "NrToWorldEvent.h"

#include <cstddef>
#include <cstdint>

namespace psnr::runtime
{
    using psnr::core::NrStatus;

    class NrGateway;
    class NrServerSnapshot;

    enum class NrToWorldWaitResult : std::uint8_t
    {
        EventsAvailable = 0, // TryPopToWorldEvents로 drain할 event가 하나 이상 있다.
        TimedOut,            // timeout 동안 event나 종료 신호가 없었다.
        Closed,              // Runtime producer 종료 후 남은 event까지 모두 drain했다.
    };

    class NrServer final
    {
    public:
        PSNR_API NrServer() noexcept;

        NrServer(const NrServer&) = delete;
        NrServer& operator=(const NrServer&) = delete;

        PSNR_API NrServer(NrServer&& other) noexcept;
        PSNR_API NrServer& operator=(NrServer&& other) noexcept;

        PSNR_API ~NrServer() noexcept;

        [[nodiscard]] PSNR_API static NrStatus Create(const NrServerConfig& config, NrServer* outServer) noexcept;
        [[nodiscard]] PSNR_API NrStatus CreateGateway(NrGateway* outGateway) noexcept;

        [[nodiscard]] PSNR_API bool IsValid() const noexcept;
        [[nodiscard]] PSNR_API NrStatus Start() noexcept;
        [[nodiscard]] PSNR_API NrStatus RequestStop() noexcept;
        [[nodiscard]] PSNR_API NrStatus Shutdown() noexcept;
        // Success는 actor close event의 admission 완료를 뜻하며 SessionClosed 처리 완료를 뜻하지 않는다.
        [[nodiscard]] PSNR_API NrStatus RequestSessionClose(NrSessionKey sessionKey,
                                                            NrSessionCloseRequestReason reason) noexcept;
        [[nodiscard]] PSNR_API NrStatus TryPopToWorldEvent(NrToWorldEvent* outEvent) noexcept;
        [[nodiscard]] PSNR_API NrStatus TryPopToWorldEvents(NrToWorldEvent* eventBuffer, std::size_t eventBufferCount,
                                                            std::size_t* outEventCount) noexcept;
        [[nodiscard]] PSNR_API NrStatus WaitForToWorldEvents(std::uint32_t timeoutMilliseconds,
                                                             NrToWorldWaitResult* outWaitResult) noexcept;
        [[nodiscard]] PSNR_API NrStatus CaptureSnapshot(NrServerSnapshot* outSnapshot) const noexcept;

    private:
        struct Impl;

        explicit NrServer(Impl* impl) noexcept;

        Impl* impl_ = nullptr;
    };
} // namespace psnr::runtime
