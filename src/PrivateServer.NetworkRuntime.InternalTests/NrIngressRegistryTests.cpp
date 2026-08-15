#include "pch.h"

#include "NrIngressRegistry.h"

#include <array>
#include <cstddef>
#include <span>
#include <utility>

namespace psnr::core
{
    namespace
    {
        constexpr NrPacketType HeartbeatPacketType{0};

        class NrRecordingIngress final : public NrIngress
        {
        public:
            explicit NrRecordingIngress(NrStatus enqueueStatus = NrStatus()) noexcept
                : enqueueStatus_(enqueueStatus)
            {
            }

            [[nodiscard]] NrStatus TryEnqueue(NrInput&& input) noexcept override
            {
                if (enqueueStatus_.Failed())
                {
                    return enqueueStatus_;
                }

                lastInput = std::move(input);
                ++enqueueCount;
                return NrStatus();
            }

            NrStatus enqueueStatus_;
            NrInput lastInput;
            std::size_t enqueueCount = 0;
        };

        [[nodiscard]] NrInput MakeInput(NrSessionKey sessionId = 1,
                                        NrDispatchLane lane = NrDispatchLane::ServerIngress) noexcept
        {
            return NrInput(sessionId, HeartbeatPacketType, lane, NrPayload{});
        }

        [[nodiscard]] NrIngressRegistry CreateRegistryWith(NrDispatchLane lane, NrIngress& ingress)
        {
            const std::array<NrIngressBinding, 1> bindings = {
                NrIngressBinding{lane, &ingress},
            };
            NrResult<NrIngressRegistry> registryResult = NrIngressRegistry::Create(std::span(bindings));
            EXPECT_TRUE(registryResult.Succeeded());
            return registryResult.TakeValue();
        }

        TEST(NrIngressRegistryTests, CreateRejectsUnknownLane)
        {
            NrRecordingIngress ingress;
            const std::array<NrIngressBinding, 1> bindings = {
                NrIngressBinding{NrDispatchLane::Count, &ingress},
            };

            const NrResult<NrIngressRegistry> registryResult = NrIngressRegistry::Create(std::span(bindings));

            EXPECT_TRUE(registryResult.Failed());
            EXPECT_EQ(registryResult.ErrorCode(), NrErrorCode::InvalidArgument);
        }

        TEST(NrIngressRegistryTests, CreateRejectsNullIngress)
        {
            const std::array<NrIngressBinding, 1> bindings = {
                NrIngressBinding{NrDispatchLane::ServerIngress, nullptr},
            };

            const NrResult<NrIngressRegistry> registryResult = NrIngressRegistry::Create(std::span(bindings));

            EXPECT_TRUE(registryResult.Failed());
            EXPECT_EQ(registryResult.ErrorCode(), NrErrorCode::InvalidArgument);
        }

        TEST(NrIngressRegistryTests, CreateRejectsDuplicateLane)
        {
            NrRecordingIngress firstIngress;
            NrRecordingIngress secondIngress;
            const std::array<NrIngressBinding, 2> bindings = {
                NrIngressBinding{NrDispatchLane::ServerIngress, &firstIngress},
                NrIngressBinding{NrDispatchLane::ServerIngress, &secondIngress},
            };

            const NrResult<NrIngressRegistry> registryResult = NrIngressRegistry::Create(std::span(bindings));

            EXPECT_TRUE(registryResult.Failed());
            EXPECT_EQ(registryResult.ErrorCode(), NrErrorCode::InvalidArgument);
        }

        TEST(NrIngressRegistryTests, TryEnqueueRejectsUnknownLane)
        {
            NrRecordingIngress ingress;
            NrIngressRegistry registry = CreateRegistryWith(NrDispatchLane::ServerIngress, ingress);
            NrInput input = MakeInput();

            const NrStatus status = registry.TryEnqueue(NrDispatchLane::Count, std::move(input));

            EXPECT_TRUE(status.Failed());
            EXPECT_EQ(status.ErrorCode(), NrErrorCode::InvalidArgument);
        }

        TEST(NrIngressRegistryTests, TryEnqueueRejectsMissingIngress)
        {
            const std::span<const NrIngressBinding> bindings;
            NrResult<NrIngressRegistry> registryResult = NrIngressRegistry::Create(bindings);
            ASSERT_TRUE(registryResult.Succeeded());
            NrIngressRegistry registry = registryResult.TakeValue();
            NrInput input = MakeInput();

            const NrStatus status = registry.TryEnqueue(NrDispatchLane::ServerIngress, std::move(input));

            EXPECT_TRUE(status.Failed());
            EXPECT_EQ(status.ErrorCode(), NrErrorCode::InvalidState);
        }

        TEST(NrIngressRegistryTests, TryEnqueueForwardsInputToRegisteredIngress)
        {
            NrRecordingIngress ingress;
            NrIngressRegistry registry = CreateRegistryWith(NrDispatchLane::ServerIngress, ingress);
            NrInput input = MakeInput(42);

            const NrStatus status = registry.TryEnqueue(NrDispatchLane::ServerIngress, std::move(input));

            EXPECT_TRUE(status.Succeeded());
            EXPECT_EQ(ingress.enqueueCount, 1u);
            EXPECT_EQ(ingress.lastInput.sessionId, 42u);
            EXPECT_EQ(ingress.lastInput.packetType, HeartbeatPacketType);
            EXPECT_EQ(ingress.lastInput.dispatchLane, NrDispatchLane::ServerIngress);
        }

        TEST(NrIngressRegistryTests, TryEnqueueForwardsSessionIngressBinding)
        {
            NrRecordingIngress ingress;
            NrIngressRegistry registry = CreateRegistryWith(NrDispatchLane::SessionIngress, ingress);
            NrInput input = MakeInput(24, NrDispatchLane::SessionIngress);

            const NrStatus status = registry.TryEnqueue(NrDispatchLane::SessionIngress, std::move(input));

            EXPECT_TRUE(status.Succeeded());
            EXPECT_EQ(ingress.enqueueCount, 1u);
            EXPECT_EQ(ingress.lastInput.sessionId, 24u);
            EXPECT_EQ(ingress.lastInput.dispatchLane, NrDispatchLane::SessionIngress);
        }

        TEST(NrIngressRegistryTests, TryEnqueuePropagatesIngressFailure)
        {
            NrRecordingIngress ingress{NrStatus(NrErrorCode::QueueFull)};
            NrIngressRegistry registry = CreateRegistryWith(NrDispatchLane::ServerIngress, ingress);
            NrInput input = MakeInput(7);

            const NrStatus status = registry.TryEnqueue(NrDispatchLane::ServerIngress, std::move(input));

            EXPECT_TRUE(status.Failed());
            EXPECT_EQ(status.ErrorCode(), NrErrorCode::QueueFull);
            EXPECT_EQ(ingress.enqueueCount, 0u);
        }
    } // namespace
} // namespace psnr::core
