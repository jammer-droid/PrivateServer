#include "pch.h"

#include "NrMemoryPoolConfigFactory.h"

namespace psnr::core
{
    namespace
    {
        [[nodiscard]] constexpr NrMemoryPoolConfig MakePoolConfig(std::size_t blockSize, std::size_t blockCount,
                                                                  std::size_t alignment = 64) noexcept
        {
            return NrMemoryPoolConfig{blockSize, blockCount, alignment};
        }
    } // namespace

    NrMemoryPoolManagerConfig NrMemoryPoolConfigFactory::CreateTinyTestConfig() noexcept
    {
        NrMemoryPoolManagerConfig config;
        config.pools = {
            NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::RecvBuffer, MakePoolConfig(64, 1)},
            NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::SendBuffer, MakePoolConfig(64, 1)},
            NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::OverlappedContext, MakePoolConfig(128, 1)},
            NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::RuntimeIngressQueueStorage, MakePoolConfig(512, 1)},
            NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::ToWorldEventQueueStorage, MakePoolConfig(512, 1)},
            NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::SessionAcceptRecvMailboxStorage,
                                          MakePoolConfig(512, 1)},
            NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::SessionSendMailboxStorage,
                                          MakePoolConfig(512, 1)},
            NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::ActorReadyQueueStorage, MakePoolConfig(512, 1)},
            NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::Payload64, MakePoolConfig(64, 1)},
            NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::Payload256, MakePoolConfig(256, 1)},
            NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::Payload1024, MakePoolConfig(1024, 1)},
            NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::Payload8192, MakePoolConfig(8192, 1)},
            NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::PayloadRefControl, MakePoolConfig(64, 1)},
        };
        return config;
    }

} // namespace psnr::core
