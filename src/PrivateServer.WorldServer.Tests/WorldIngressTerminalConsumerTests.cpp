#include "pch.h"

#include "WorldIngressTerminalConsumer.h"

#include <PrivateServer/NetworkRuntime/NrToWorldEvent.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace psnr::world::tests
{
    namespace
    {
        struct FakeTerminalEvent final
        {
            psnr::runtime::NrToWorldEventKind kind = psnr::runtime::NrToWorldEventKind::None;

            [[nodiscard]] psnr::runtime::NrToWorldEventKind Kind() const noexcept
            {
                return kind;
            }
        };

        class FakeTerminalEventConsumer final
        {
        public:
            [[nodiscard]] std::uint8_t Handle(const FakeTerminalEvent& event,
                                              const WorldInboundMode inboundMode) noexcept
            {
                handledKinds_.push_back(event.Kind());
                inboundMode_ = inboundMode;
                return 0;
            }

            [[nodiscard]] const std::vector<psnr::runtime::NrToWorldEventKind>& HandledKinds() const noexcept
            {
                return handledKinds_;
            }

            [[nodiscard]] WorldInboundMode InboundMode() const noexcept
            {
                return inboundMode_;
            }

        private:
            std::vector<psnr::runtime::NrToWorldEventKind> handledKinds_;
            WorldInboundMode inboundMode_ = WorldInboundMode::TargetServerTick;
        };
    } // namespace

    TEST(WorldIngressTerminalConsumerTests, ForwardsLifecycleAndDiscardsPacketsWithoutDecode)
    {
        const std::array<FakeTerminalEvent, 4> events{
            FakeTerminalEvent{psnr::runtime::NrToWorldEventKind::SessionAccepted},
            FakeTerminalEvent{psnr::runtime::NrToWorldEventKind::PacketReceived},
            FakeTerminalEvent{psnr::runtime::NrToWorldEventKind::SessionClosed},
            FakeTerminalEvent{psnr::runtime::NrToWorldEventKind::None},
        };
        FakeTerminalEventConsumer consumer;

        const WorldIngressTerminalConsumeReport report =
            WorldIngressTerminalConsumer::Consume(std::span<const FakeTerminalEvent>{events}, consumer);

        EXPECT_EQ(report.acceptedEventCount, 1u);
        EXPECT_EQ(report.closedEventCount, 1u);
        EXPECT_EQ(report.discardedPacketCount, 1u);
        EXPECT_EQ(report.unsupportedEventCount, 1u);
        ASSERT_EQ(consumer.HandledKinds().size(), 2u);
        EXPECT_EQ(consumer.HandledKinds()[0], psnr::runtime::NrToWorldEventKind::SessionAccepted);
        EXPECT_EQ(consumer.HandledKinds()[1], psnr::runtime::NrToWorldEventKind::SessionClosed);
        EXPECT_EQ(consumer.InboundMode(), WorldInboundMode::DoubleBuffered);
    }
} // namespace psnr::world::tests
