#pragma once

#include <PrivateServer/NetworkRuntime/NrClient.h>
#include <PrivateServer/NetworkRuntime/NrEndpoint.h>

#include "RoundResult.h"
#include "WorldProtocolValues.h"

#include <cstdint>
#include <string>

namespace psnr::benchmark
{
    class BenchmarkWorldClient final
    {
    public:
        BenchmarkWorldClient() noexcept = default;

        BenchmarkWorldClient(const BenchmarkWorldClient&) = delete;
        BenchmarkWorldClient& operator=(const BenchmarkWorldClient&) = delete;

        BenchmarkWorldClient(BenchmarkWorldClient&&) noexcept = default;
        BenchmarkWorldClient& operator=(BenchmarkWorldClient&&) noexcept = default;

        [[nodiscard]] std::string Start(const psnr::runtime::NrEndpoint& endpoint,
                                        const psnr::runtime::NrClientConfig& clientConfig,
                                        std::uint32_t joinTimeoutMilliseconds);
        [[nodiscard]] std::string Shutdown() noexcept;
        [[nodiscard]] std::string DrainAvailableEvents();
        [[nodiscard]] std::string SendControlState(psnr::world::protocol::v2::TurnState turnState,
                                                   psnr::world::protocol::v2::BoostState boostState);

        [[nodiscard]] bool Joined() const noexcept;
        [[nodiscard]] std::uint32_t PlayerId() const noexcept;
        [[nodiscard]] std::uint32_t ControlledEntityId() const noexcept;
        [[nodiscard]] std::uint32_t ControlledEntityGeneration() const noexcept;
        [[nodiscard]] bool HasRoundResult() const noexcept;
        [[nodiscard]] const psnr::world::protocol::v2::RoundResult& CommittedRoundResult() const noexcept;

    private:
        psnr::runtime::NrClient client_;
        std::uint32_t playerId_ = 0;
        std::uint32_t controlledEntityId_ = 0;
        std::uint32_t controlledEntityGeneration_ = 0;
        std::uint32_t nextControlSequence_ = 1;
        psnr::world::protocol::v2::RoundResult roundResult_;
        bool joined_ = false;
        bool roundResultCommitted_ = false;
        bool resultConnectionClosed_ = false;
    };
} // namespace psnr::benchmark
