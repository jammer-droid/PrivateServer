#pragma once

#include "NrMemoryPool.h"
#include "NrResult.h"

#include <array>
#include <cstddef>
#include <memory>

namespace psnr::core
{
    class NrPayloadRefFactory;

    enum class NrMemoryPoolRole
    {
        RecvBuffer,
        SendBuffer,
        OverlappedContext,
        RuntimeIngressQueueStorage,
        ToWorldEventQueueStorage,
        SessionAcceptRecvMailboxStorage,
        SessionSendMailboxStorage,
        ActorReadyQueueStorage,
        DiagnosticsQueueStorage,

        Payload64,
        Payload256,
        Payload1024,
        Payload8192,
        PayloadRefControl,

        ClientPayloadQueueStorage,
        ClientEventQueueStorage,

        Count,
    };

    inline constexpr std::size_t NrMemoryPoolRoleCount = static_cast<std::size_t>(NrMemoryPoolRole::Count);

    struct NrMemoryPoolManagerPoolConfig
    {
        NrMemoryPoolRole role = NrMemoryPoolRole::Count;
        NrMemoryPoolConfig pool{};
    };

    struct NrMemoryPoolManagerConfig
    {
        std::array<NrMemoryPoolManagerPoolConfig, NrMemoryPoolRoleCount> pools;
    };

    struct NrMemoryPoolManagerStatsEntry
    {
        NrMemoryPoolRole role;
        NrMemoryPoolStats stats;
    };

    struct NrMemoryPoolManagerStats
    {
        std::array<NrMemoryPoolManagerStatsEntry, NrMemoryPoolRoleCount> pools;
    };

    class NrMemoryPoolManager
    {
        friend class NrPayloadRefFactory;

    public:
        NrMemoryPoolManager(const NrMemoryPoolManager&) = delete;
        NrMemoryPoolManager& operator=(const NrMemoryPoolManager&) = delete;

        NrMemoryPoolManager(NrMemoryPoolManager&&) = delete;
        NrMemoryPoolManager& operator=(NrMemoryPoolManager&&) = delete;

        ~NrMemoryPoolManager() noexcept;

        [[nodiscard]] static NrResult<std::unique_ptr<NrMemoryPoolManager>> Create(
            const NrMemoryPoolManagerConfig& config);

        [[nodiscard]] NrResult<NrPooledMemoryBlock> AcquireBlock(NrMemoryPoolRole role) noexcept;

        [[nodiscard]] NrResult<NrMemoryPoolStats> Stats(NrMemoryPoolRole role) const noexcept;
        [[nodiscard]] NrMemoryPoolManagerStats Stats() const noexcept;

    private:
        using NrPoolArray = std::array<std::unique_ptr<NrMemoryPool>, NrMemoryPoolRoleCount>;

        explicit NrMemoryPoolManager(NrPoolArray pools) noexcept;

        [[nodiscard]] NrMemoryPool* ResolvePool(NrMemoryPoolRole role) noexcept;
        [[nodiscard]] const NrMemoryPool* ResolvePool(NrMemoryPoolRole role) const noexcept;
        [[nodiscard]] static bool TryGetPoolIndex(NrMemoryPoolRole role, std::size_t& index) noexcept;
        [[nodiscard]] static bool IsKnownRole(NrMemoryPoolRole role) noexcept;

    private:
        NrPoolArray pools_;
    };

} // namespace psnr::core
