#pragma once

#include "WorldApplicationEventSink.h"

#include <array>
#include <cstddef>

namespace psnr::world::tests
{
    class WorldApplicationEventSinkTestDouble final : public IWorldApplicationEventSink
    {
    public:
        void RecordJoin(const WorldJoinApplicationEvent& event) noexcept override
        {
            if (joinEventCount_ < joinEvents_.size())
            {
                joinEvents_[joinEventCount_] = event;
                ++joinEventCount_;
            }
        }

        void RecordSessionCleanup(const WorldSessionCleanupApplicationEvent& event) noexcept override
        {
            if (sessionCleanupEventCount_ < sessionCleanupEvents_.size())
            {
                sessionCleanupEvents_[sessionCleanupEventCount_] = event;
                ++sessionCleanupEventCount_;
            }
        }

        void RecordProtocolClose(const WorldProtocolCloseApplicationEvent& event) noexcept override
        {
            if (protocolCloseEventCount_ < protocolCloseEvents_.size())
            {
                protocolCloseEvents_[protocolCloseEventCount_] = event;
                ++protocolCloseEventCount_;
            }
        }

        void Clear() noexcept
        {
            joinEventCount_ = 0;
            sessionCleanupEventCount_ = 0;
            protocolCloseEventCount_ = 0;
        }

        [[nodiscard]] std::size_t JoinEventCount() const noexcept
        {
            return joinEventCount_;
        }

        [[nodiscard]] const WorldJoinApplicationEvent& JoinEvent(const std::size_t index) const noexcept
        {
            return joinEvents_[index];
        }

        [[nodiscard]] std::size_t SessionCleanupEventCount() const noexcept
        {
            return sessionCleanupEventCount_;
        }

        [[nodiscard]] const WorldSessionCleanupApplicationEvent& SessionCleanupEvent(
            const std::size_t index) const noexcept
        {
            return sessionCleanupEvents_[index];
        }

        [[nodiscard]] std::size_t ProtocolCloseEventCount() const noexcept
        {
            return protocolCloseEventCount_;
        }

        [[nodiscard]] const WorldProtocolCloseApplicationEvent& ProtocolCloseEvent(
            const std::size_t index) const noexcept
        {
            return protocolCloseEvents_[index];
        }

    private:
        std::array<WorldJoinApplicationEvent, 32> joinEvents_{};
        std::size_t joinEventCount_ = 0;
        std::array<WorldSessionCleanupApplicationEvent, 32> sessionCleanupEvents_{};
        std::size_t sessionCleanupEventCount_ = 0;
        std::array<WorldProtocolCloseApplicationEvent, 32> protocolCloseEvents_{};
        std::size_t protocolCloseEventCount_ = 0;
    };
} // namespace psnr::world::tests
