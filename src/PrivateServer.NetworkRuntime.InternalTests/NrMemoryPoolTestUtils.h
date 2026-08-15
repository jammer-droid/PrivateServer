#pragma once

#include "NrMemoryPoolManager.h"

#include "gtest/gtest.h"

#include <cstddef>
#include <memory>

namespace psnr::core::test
{
    [[nodiscard]] inline NrMemoryPoolConfig MakePoolConfig(std::size_t blockSize, std::size_t blockCount = 1,
                                                           std::size_t alignment = 16)
    {
        NrMemoryPoolConfig config;
        config.blockSize = blockSize;
        config.blockCount = blockCount;
        config.alignment = alignment;
        return config;
    }

    [[nodiscard]] inline NrMemoryPoolManagerConfig MakeDefaultMemoryPoolManagerConfig()
    {
        NrMemoryPoolManagerConfig config;
        config.pools = {
            NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::RecvBuffer, MakePoolConfig(64)},
            NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::SendBuffer, MakePoolConfig(64)},
            NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::OverlappedContext, MakePoolConfig(128)},
            NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::RuntimeIngressQueueStorage, MakePoolConfig(512)},
            NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::ToWorldEventQueueStorage, MakePoolConfig(512)},
            NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::SessionAcceptRecvMailboxStorage, MakePoolConfig(512)},
            NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::SessionSendMailboxStorage, MakePoolConfig(512)},
            NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::ActorReadyQueueStorage, MakePoolConfig(512)},
            NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::DiagnosticsQueueStorage, MakePoolConfig(64 * 1024)},
            NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::Payload64, MakePoolConfig(64)},
            NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::Payload256, MakePoolConfig(256)},
            NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::Payload1024, MakePoolConfig(1024)},
            NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::Payload8192, MakePoolConfig(8192)},
            NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::PayloadRefControl, MakePoolConfig(64)},
        };
        return config;
    }

    [[nodiscard]] inline bool SetPoolConfig(NrMemoryPoolManagerConfig& config, NrMemoryPoolRole role,
                                            NrMemoryPoolConfig poolConfig) noexcept
    {
        const std::size_t roleIndex = static_cast<std::size_t>(role);
        if (roleIndex >= NrMemoryPoolRoleCount)
        {
            return false;
        }

        config.pools[roleIndex] = NrMemoryPoolManagerPoolConfig{role, poolConfig};
        return true;
    }

    [[nodiscard]] inline NrMemoryPoolManagerConfig MakeMemoryPoolManagerConfigWith(NrMemoryPoolRole role,
                                                                                   NrMemoryPoolConfig poolConfig)
    {
        NrMemoryPoolManagerConfig config = MakeDefaultMemoryPoolManagerConfig();
        EXPECT_TRUE(SetPoolConfig(config, role, poolConfig));
        return config;
    }

    [[nodiscard]] inline std::unique_ptr<NrMemoryPoolManager> CreateMemoryPoolManager(
        const NrMemoryPoolManagerConfig& config)
    {
        NrResult<std::unique_ptr<NrMemoryPoolManager>> createResult = NrMemoryPoolManager::Create(config);
        EXPECT_TRUE(createResult.Succeeded());
        if (createResult.Failed())
        {
            return nullptr;
        }

        return createResult.TakeValue();
    }

    [[nodiscard]] inline std::unique_ptr<NrMemoryPoolManager> CreateMemoryPoolManager()
    {
        return CreateMemoryPoolManager(MakeDefaultMemoryPoolManagerConfig());
    }

    [[nodiscard]] inline NrMemoryPoolStats Stats(NrMemoryPoolManager& manager, NrMemoryPoolRole role)
    {
        NrResult<NrMemoryPoolStats> statsResult = manager.Stats(role);
        EXPECT_TRUE(statsResult.Succeeded());
        if (statsResult.Failed())
        {
            return NrMemoryPoolStats{};
        }

        return statsResult.TakeValue();
    }
} // namespace psnr::core::test
