#pragma once

#include "NrResult.h"
#include "NrSession.h"
#include "NrSessionKey.h"
#include "NrStatus.h"

#include <cstddef>
#include <unordered_map>

namespace psnr::runtime
{
    using psnr::core::NrResult;
    using psnr::core::NrSessionKey;
    using psnr::core::NrStatus;

    // [DEPRECATED] Legacy pre-actor session owner.
    // PRI-32 accept-to-first-recv must use NrSessionIoActor/NrSessionActorRegistry instead.
    class NrSessionRegistry final
    {
    public:
        // Not thread-safe. The owning runtime path must serialize access.
        NrSessionRegistry() noexcept = default;

        NrSessionRegistry(const NrSessionRegistry&) = delete;
        NrSessionRegistry& operator=(const NrSessionRegistry&) = delete;

        NrSessionRegistry(NrSessionRegistry&&) noexcept = default;
        NrSessionRegistry& operator=(NrSessionRegistry&&) noexcept = default;

        ~NrSessionRegistry() noexcept = default;

        [[nodiscard]] NrStatus TryRegister(NrSession&& session) noexcept;
        [[nodiscard]] NrSession* Find(NrSessionKey sessionKey) noexcept;
        [[nodiscard]] const NrSession* Find(NrSessionKey sessionKey) const noexcept;
        [[nodiscard]] NrResult<NrSession> Remove(NrSessionKey sessionKey) noexcept;

        [[nodiscard]] std::size_t Count() const noexcept;

    private:
        using NrSessionMap = std::unordered_map<NrSessionKey, NrSession>;

        NrSessionMap sessions_;
    };
} // namespace psnr::runtime
