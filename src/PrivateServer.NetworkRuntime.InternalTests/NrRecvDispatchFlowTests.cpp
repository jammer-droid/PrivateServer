#include "pch.h"

#include "NrBoundedMpscQueue.h"
#include "NrIngressRegistry.h"
#include "NrInputFactory.h"
#include "NrMemoryPoolTestUtils.h"
#include "NrPacketDispatchTable.h"
#include "NrPacketHeader.h"
#include "NrPacketParser.h"
#include "NrQueueIngress.h"
#include "NrRecvBuffer.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace psnr::core
{
    namespace
    {
        constexpr NrPacketType HeartbeatPacketType{0};
        constexpr NrPacketType MoveInputPacketType{1};

        using NrInputQueue = NrBoundedMpscQueue<NrInput>;

        struct NrRecvDispatchFlowResult
        {
            NrStatus status;
            NrPacketParseStatus parseStatus = NrPacketParseStatus::NeedMoreData;
        };

        struct NrRecvDispatchDrainResult
        {
            NrStatus status;
            NrPacketParseStatus lastParseStatus = NrPacketParseStatus::NeedMoreData;
            std::size_t completedCount = 0;
        };

        [[nodiscard]] std::vector<std::byte> MakePacketBytes(std::uint16_t rawPacketType,
                                                             std::span<const std::byte> payloadBytes = {})
        {
            const std::uint16_t packetLength = static_cast<std::uint16_t>(NrPacketHeaderLength + payloadBytes.size());
            std::vector<std::byte> packetBytes;
            packetBytes.reserve(packetLength);
            packetBytes.push_back(static_cast<std::byte>(packetLength & 0xFF));
            packetBytes.push_back(static_cast<std::byte>((packetLength >> 8) & 0xFF));
            packetBytes.push_back(static_cast<std::byte>(rawPacketType & 0xFF));
            packetBytes.push_back(static_cast<std::byte>((rawPacketType >> 8) & 0xFF));
            packetBytes.push_back(std::byte{1}); // version
            packetBytes.push_back(std::byte{0}); // flags
            packetBytes.insert(packetBytes.end(), payloadBytes.begin(), payloadBytes.end());
            return packetBytes;
        }

        [[nodiscard]] std::vector<std::byte> MakePacketBytes(NrPacketType packetType,
                                                             std::span<const std::byte> payloadBytes = {})
        {
            return MakePacketBytes(packetType.value, payloadBytes);
        }

        [[nodiscard]] NrMemoryPoolManagerConfig MakeFlowTestManagerConfig(std::size_t queueCapacity = 2,
                                                                          std::size_t payload64BlockCount = 1)
        {
            const std::size_t queueStorageBytes = sizeof(NrBoundedMpscQueueSlot<NrInput>) * queueCapacity;
            NrMemoryPoolManagerConfig config = test::MakeDefaultMemoryPoolManagerConfig();
            EXPECT_TRUE(test::SetPoolConfig(config, NrMemoryPoolRole::RuntimeIngressQueueStorage,
                                            test::MakePoolConfig(queueStorageBytes, 1, NrCacheLineSize)));
            EXPECT_TRUE(test::SetPoolConfig(config, NrMemoryPoolRole::Payload64,
                                            test::MakePoolConfig(64, payload64BlockCount, NrCacheLineSize)));
            return config;
        }

        [[nodiscard]] std::unique_ptr<NrMemoryPoolManager> CreateFlowTestManager(std::size_t queueCapacity = 2,
                                                                                 std::size_t payload64BlockCount = 1)
        {
            return test::CreateMemoryPoolManager(MakeFlowTestManagerConfig(queueCapacity, payload64BlockCount));
        }

        [[nodiscard]] NrPacketParser CreateParser()
        {
            NrResult<NrPacketParser> parserResult = NrPacketParser::Create(NrPacketParserConfig{});
            EXPECT_TRUE(parserResult.Succeeded());
            return parserResult.TakeValue();
        }

        [[nodiscard]] NrPacketDispatchTable CreateHeartbeatDispatchTable()
        {
            const std::array<NrPacketDispatchRule, 1> rules = {
                NrPacketDispatchRule{HeartbeatPacketType, NrDispatchLane::ServerIngress},
            };

            NrResult<NrPacketDispatchTable> tableResult = NrPacketDispatchTable::Create(std::span(rules));
            EXPECT_TRUE(tableResult.Succeeded());
            return tableResult.TakeValue();
        }

        [[nodiscard]] NrPacketDispatchTable CreateServerAndWorldDispatchTable()
        {
            const std::array<NrPacketDispatchRule, 2> rules = {
                NrPacketDispatchRule{HeartbeatPacketType, NrDispatchLane::ServerIngress},
                NrPacketDispatchRule{MoveInputPacketType, NrDispatchLane::WorldIngress},
            };

            NrResult<NrPacketDispatchTable> tableResult = NrPacketDispatchTable::Create(std::span(rules));
            EXPECT_TRUE(tableResult.Succeeded());
            return tableResult.TakeValue();
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

        [[nodiscard]] NrIngressRegistry CreateRegistryWith(NrIngress& ingress)
        {
            return CreateRegistryWith(NrDispatchLane::ServerIngress, ingress);
        }

        [[nodiscard]] NrIngressRegistry CreateServerAndWorldRegistryWith(NrIngress& ingress)
        {
            const std::array<NrIngressBinding, 2> bindings = {
                NrIngressBinding{NrDispatchLane::ServerIngress, &ingress},
                NrIngressBinding{NrDispatchLane::WorldIngress, &ingress},
            };

            NrResult<NrIngressRegistry> registryResult = NrIngressRegistry::Create(std::span(bindings));
            EXPECT_TRUE(registryResult.Succeeded());
            return registryResult.TakeValue();
        }

        [[nodiscard]] NrIngressRegistry CreateEmptyRegistry()
        {
            const std::span<const NrIngressBinding> bindings;
            NrResult<NrIngressRegistry> registryResult = NrIngressRegistry::Create(bindings);
            EXPECT_TRUE(registryResult.Succeeded());
            return registryResult.TakeValue();
        }

        [[nodiscard]] std::unique_ptr<NrInputQueue> CreateInputQueue(NrMemoryPoolManager& manager)
        {
            NrResult<std::unique_ptr<NrInputQueue>> queueResult =
                NrInputQueue::Create(manager, NrMemoryPoolRole::RuntimeIngressQueueStorage, 2);
            EXPECT_TRUE(queueResult.Succeeded());
            if (queueResult.Failed())
            {
                return nullptr;
            }

            return queueResult.TakeValue();
        }

        [[nodiscard]] NrRecvBuffer CreateRecvBuffer(NrMemoryPoolManager& manager, std::span<const std::byte> bytes)
        {
            NrResult<NrRecvBuffer> bufferResult = NrRecvBuffer::Create(manager, 64);
            EXPECT_TRUE(bufferResult.Succeeded());
            NrRecvBuffer recvBuffer = bufferResult.TakeValue();

            std::copy(bytes.begin(), bytes.end(), recvBuffer.WritableSpan().begin());
            EXPECT_TRUE(recvBuffer.CommitWritten(bytes.size()).Succeeded());
            return recvBuffer;
        }

        [[nodiscard]] NrRecvDispatchFlowResult DrainOnePacket(NrSessionKey sessionId, NrRecvBuffer& recvBuffer,
                                                              const NrPacketParser& parser,
                                                              const NrPacketDispatchTable& dispatchTable,
                                                              NrInputFactory& inputFactory,
                                                              NrIngressRegistry& ingressRegistry) noexcept
        {
            NrPacketParseResult parseResult;
            const NrStatus parseStatus = parser.Parse(recvBuffer.ReadableSpan(), parseResult);
            if (parseStatus.Failed())
            {
                return NrRecvDispatchFlowResult{parseStatus, parseResult.status};
            }

            if (parseResult.status != NrPacketParseStatus::Complete)
            {
                return NrRecvDispatchFlowResult{NrStatus(), parseResult.status};
            }

            NrPacketDispatchRule dispatchRule;
            const NrStatus dispatchRuleStatus =
                dispatchTable.Find(NrPacketType{parseResult.header.packetType}, dispatchRule);
            if (dispatchRuleStatus.Failed())
            {
                return NrRecvDispatchFlowResult{dispatchRuleStatus, parseResult.status};
            }

            NrResult<NrInput> inputResult = inputFactory.CreateInput(sessionId, parseResult, dispatchRule);
            if (inputResult.Failed())
            {
                return NrRecvDispatchFlowResult{inputResult.Status(), parseResult.status};
            }

            NrInput input = inputResult.TakeValue();
            const NrStatus enqueueStatus = ingressRegistry.TryEnqueue(dispatchRule.dispatchLane, std::move(input));
            if (enqueueStatus.Failed())
            {
                return NrRecvDispatchFlowResult{enqueueStatus, parseResult.status};
            }

            const NrStatus consumeStatus = recvBuffer.Consume(parseResult.header.packetLength);
            return NrRecvDispatchFlowResult{consumeStatus, parseResult.status};
        }

        [[nodiscard]] NrRecvDispatchDrainResult DrainAvailablePackets(NrSessionKey sessionId, NrRecvBuffer& recvBuffer,
                                                                      const NrPacketParser& parser,
                                                                      const NrPacketDispatchTable& dispatchTable,
                                                                      NrInputFactory& inputFactory,
                                                                      NrIngressRegistry& ingressRegistry) noexcept
        {
            NrRecvDispatchDrainResult drainResult;
            while (recvBuffer.ReadableBytes() > 0)
            {
                const NrRecvDispatchFlowResult packetResult =
                    DrainOnePacket(sessionId, recvBuffer, parser, dispatchTable, inputFactory, ingressRegistry);
                drainResult.status = packetResult.status;
                drainResult.lastParseStatus = packetResult.parseStatus;

                if (packetResult.status.Failed() || packetResult.parseStatus != NrPacketParseStatus::Complete)
                {
                    return drainResult;
                }

                ++drainResult.completedCount;
            }

            return drainResult;
        }

        TEST(NrRecvDispatchFlowTests, CompleteHeartbeatEnqueuesInputAndConsumesFrame)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = CreateFlowTestManager();
            ASSERT_NE(manager, nullptr);
            std::unique_ptr<NrInputQueue> queue = CreateInputQueue(*manager);
            ASSERT_NE(queue, nullptr);
            NrQueueIngress ingress(*queue);
            NrIngressRegistry registry = CreateRegistryWith(ingress);
            const NrPacketParser parser = CreateParser();
            const NrPacketDispatchTable dispatchTable = CreateHeartbeatDispatchTable();
            NrInputFactory inputFactory(*manager);
            const std::array<std::byte, 3> payloadBytes = {std::byte{0x11}, std::byte{0x22}, std::byte{0x33}};
            const std::vector<std::byte> packetBytes =
                MakePacketBytes(HeartbeatPacketType, std::span(payloadBytes));
            NrRecvBuffer recvBuffer = CreateRecvBuffer(*manager, std::span(packetBytes));

            const NrRecvDispatchFlowResult result =
                DrainOnePacket(42, recvBuffer, parser, dispatchTable, inputFactory, registry);

            EXPECT_TRUE(result.status.Succeeded());
            EXPECT_EQ(result.parseStatus, NrPacketParseStatus::Complete);
            EXPECT_EQ(recvBuffer.ReadableBytes(), 0u);
            EXPECT_EQ(recvBuffer.ConsumedBytes(), packetBytes.size());
            EXPECT_EQ(queue->SizeApprox(), 1u);

            NrInput output;
            ASSERT_TRUE(queue->TryPop(output).Succeeded());
            EXPECT_EQ(output.sessionId, 42u);
            EXPECT_EQ(output.packetType, HeartbeatPacketType);
            EXPECT_EQ(output.dispatchLane, NrDispatchLane::ServerIngress);
            EXPECT_EQ(output.payload.Length(), payloadBytes.size());
            EXPECT_TRUE(std::ranges::equal(output.payload.Bytes(), payloadBytes));
        }

        TEST(NrRecvDispatchFlowTests, CompleteMoveInputEnqueuesWorldIngressInputAndConsumesFrame)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = CreateFlowTestManager();
            ASSERT_NE(manager, nullptr);
            std::unique_ptr<NrInputQueue> queue = CreateInputQueue(*manager);
            ASSERT_NE(queue, nullptr);
            NrQueueIngress ingress(*queue);
            NrIngressRegistry registry = CreateRegistryWith(NrDispatchLane::WorldIngress, ingress);
            const NrPacketParser parser = CreateParser();
            const NrPacketDispatchTable dispatchTable = CreateServerAndWorldDispatchTable();
            NrInputFactory inputFactory(*manager);
            const std::array<std::byte, 4> payloadBytes = {
                std::byte{0x10},
                std::byte{0x20},
                std::byte{0x30},
                std::byte{0x40},
            };
            const std::vector<std::byte> packetBytes =
                MakePacketBytes(MoveInputPacketType, std::span(payloadBytes));
            NrRecvBuffer recvBuffer = CreateRecvBuffer(*manager, std::span(packetBytes));

            const NrRecvDispatchFlowResult result =
                DrainOnePacket(77, recvBuffer, parser, dispatchTable, inputFactory, registry);

            EXPECT_TRUE(result.status.Succeeded());
            EXPECT_EQ(result.parseStatus, NrPacketParseStatus::Complete);
            EXPECT_EQ(recvBuffer.ReadableBytes(), 0u);
            EXPECT_EQ(recvBuffer.ConsumedBytes(), packetBytes.size());
            EXPECT_EQ(queue->SizeApprox(), 1u);

            NrInput output;
            ASSERT_TRUE(queue->TryPop(output).Succeeded());
            EXPECT_EQ(output.sessionId, 77u);
            EXPECT_EQ(output.packetType, MoveInputPacketType);
            EXPECT_EQ(output.dispatchLane, NrDispatchLane::WorldIngress);
            EXPECT_EQ(output.payload.Length(), payloadBytes.size());
            EXPECT_TRUE(std::ranges::equal(output.payload.Bytes(), payloadBytes));
        }

        TEST(NrRecvDispatchFlowTests, MultipleCompleteFramesDrainInOrderFromOneFakeCompletion)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = CreateFlowTestManager(2, 2);
            ASSERT_NE(manager, nullptr);
            std::unique_ptr<NrInputQueue> queue = CreateInputQueue(*manager);
            ASSERT_NE(queue, nullptr);
            NrQueueIngress ingress(*queue);
            NrIngressRegistry registry = CreateServerAndWorldRegistryWith(ingress);
            const NrPacketParser parser = CreateParser();
            const NrPacketDispatchTable dispatchTable = CreateServerAndWorldDispatchTable();
            NrInputFactory inputFactory(*manager);
            const std::array<std::byte, 1> heartbeatPayload = {std::byte{0x11}};
            const std::array<std::byte, 1> movePayload = {std::byte{0x22}};
            std::vector<std::byte> packetBytes = MakePacketBytes(HeartbeatPacketType, std::span(heartbeatPayload));
            const std::vector<std::byte> moveBytes = MakePacketBytes(MoveInputPacketType, std::span(movePayload));
            packetBytes.insert(packetBytes.end(), moveBytes.begin(), moveBytes.end());
            NrRecvBuffer recvBuffer = CreateRecvBuffer(*manager, std::span(packetBytes));

            const NrRecvDispatchDrainResult result =
                DrainAvailablePackets(91, recvBuffer, parser, dispatchTable, inputFactory, registry);

            EXPECT_TRUE(result.status.Succeeded());
            EXPECT_EQ(result.lastParseStatus, NrPacketParseStatus::Complete);
            EXPECT_EQ(result.completedCount, 2u);
            EXPECT_EQ(recvBuffer.ReadableBytes(), 0u);
            EXPECT_EQ(recvBuffer.ConsumedBytes(), packetBytes.size());
            EXPECT_EQ(queue->SizeApprox(), 2u);

            NrInput firstOutput;
            ASSERT_TRUE(queue->TryPop(firstOutput).Succeeded());
            EXPECT_EQ(firstOutput.sessionId, 91u);
            EXPECT_EQ(firstOutput.packetType, HeartbeatPacketType);
            EXPECT_EQ(firstOutput.dispatchLane, NrDispatchLane::ServerIngress);
            EXPECT_TRUE(std::ranges::equal(firstOutput.payload.Bytes(), heartbeatPayload));

            NrInput secondOutput;
            ASSERT_TRUE(queue->TryPop(secondOutput).Succeeded());
            EXPECT_EQ(secondOutput.sessionId, 91u);
            EXPECT_EQ(secondOutput.packetType, MoveInputPacketType);
            EXPECT_EQ(secondOutput.dispatchLane, NrDispatchLane::WorldIngress);
            EXPECT_TRUE(std::ranges::equal(secondOutput.payload.Bytes(), movePayload));
        }

        TEST(NrRecvDispatchFlowTests, MalformedSecondFrameStopsDrainAfterConsumingFirstFrame)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = CreateFlowTestManager();
            ASSERT_NE(manager, nullptr);
            std::unique_ptr<NrInputQueue> queue = CreateInputQueue(*manager);
            ASSERT_NE(queue, nullptr);
            NrQueueIngress ingress(*queue);
            NrIngressRegistry registry = CreateRegistryWith(ingress);
            const NrPacketParser parser = CreateParser();
            const NrPacketDispatchTable dispatchTable = CreateHeartbeatDispatchTable();
            NrInputFactory inputFactory(*manager);
            const std::array<std::byte, 1> heartbeatPayload = {std::byte{0x33}};
            std::vector<std::byte> packetBytes = MakePacketBytes(HeartbeatPacketType, std::span(heartbeatPayload));
            const std::vector<std::byte> malformedBytes = {
                std::byte{0x05}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x01}, std::byte{0x00},
            };
            packetBytes.insert(packetBytes.end(), malformedBytes.begin(), malformedBytes.end());
            NrRecvBuffer recvBuffer = CreateRecvBuffer(*manager, std::span(packetBytes));

            const NrRecvDispatchDrainResult result =
                DrainAvailablePackets(92, recvBuffer, parser, dispatchTable, inputFactory, registry);

            EXPECT_TRUE(result.status.Failed());
            EXPECT_EQ(result.status.ErrorCode(), NrErrorCode::ProtocolError);
            EXPECT_EQ(result.lastParseStatus, NrPacketParseStatus::ProtocolError);
            EXPECT_EQ(result.completedCount, 1u);
            EXPECT_EQ(recvBuffer.ReadableBytes(), malformedBytes.size());
            EXPECT_EQ(recvBuffer.ConsumedBytes(), packetBytes.size() - malformedBytes.size());
            EXPECT_EQ(queue->SizeApprox(), 1u);

            NrInput output;
            ASSERT_TRUE(queue->TryPop(output).Succeeded());
            EXPECT_EQ(output.sessionId, 92u);
            EXPECT_EQ(output.packetType, HeartbeatPacketType);
            EXPECT_EQ(output.dispatchLane, NrDispatchLane::ServerIngress);
            EXPECT_TRUE(std::ranges::equal(output.payload.Bytes(), heartbeatPayload));
        }

        TEST(NrRecvDispatchFlowTests, UnknownPacketTypeDoesNotConsumeFrame)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = CreateFlowTestManager();
            ASSERT_NE(manager, nullptr);
            std::unique_ptr<NrInputQueue> queue = CreateInputQueue(*manager);
            ASSERT_NE(queue, nullptr);
            NrQueueIngress ingress(*queue);
            NrIngressRegistry registry = CreateRegistryWith(ingress);
            const NrPacketParser parser = CreateParser();
            const NrPacketDispatchTable dispatchTable = CreateHeartbeatDispatchTable();
            NrInputFactory inputFactory(*manager);
            const std::vector<std::byte> packetBytes = MakePacketBytes(99);
            NrRecvBuffer recvBuffer = CreateRecvBuffer(*manager, std::span(packetBytes));

            const NrRecvDispatchFlowResult result =
                DrainOnePacket(1, recvBuffer, parser, dispatchTable, inputFactory, registry);

            EXPECT_TRUE(result.status.Failed());
            EXPECT_EQ(result.status.ErrorCode(), NrErrorCode::DispatchRuleNotFound);
            EXPECT_EQ(result.parseStatus, NrPacketParseStatus::Complete);
            EXPECT_EQ(recvBuffer.ReadableBytes(), packetBytes.size());
            EXPECT_EQ(recvBuffer.ConsumedBytes(), 0u);
            EXPECT_EQ(queue->SizeApprox(), 0u);
        }

        TEST(NrRecvDispatchFlowTests, MissingIngressDoesNotConsumeFrame)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = CreateFlowTestManager();
            ASSERT_NE(manager, nullptr);
            NrIngressRegistry registry = CreateEmptyRegistry();
            const NrPacketParser parser = CreateParser();
            const NrPacketDispatchTable dispatchTable = CreateHeartbeatDispatchTable();
            NrInputFactory inputFactory(*manager);
            const std::vector<std::byte> packetBytes = MakePacketBytes(HeartbeatPacketType);
            NrRecvBuffer recvBuffer = CreateRecvBuffer(*manager, std::span(packetBytes));

            const NrRecvDispatchFlowResult result =
                DrainOnePacket(1, recvBuffer, parser, dispatchTable, inputFactory, registry);

            EXPECT_TRUE(result.status.Failed());
            EXPECT_EQ(result.status.ErrorCode(), NrErrorCode::InvalidState);
            EXPECT_EQ(result.parseStatus, NrPacketParseStatus::Complete);
            EXPECT_EQ(recvBuffer.ReadableBytes(), packetBytes.size());
            EXPECT_EQ(recvBuffer.ConsumedBytes(), 0u);
        }

        TEST(NrRecvDispatchFlowTests, QueueFullDoesNotConsumeFrame)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = CreateFlowTestManager();
            ASSERT_NE(manager, nullptr);
            std::unique_ptr<NrInputQueue> queue = CreateInputQueue(*manager);
            ASSERT_NE(queue, nullptr);
            ASSERT_TRUE(queue->TryPush(NrInput(1, HeartbeatPacketType, NrDispatchLane::ServerIngress, NrPayload{}))
                            .Succeeded());
            ASSERT_TRUE(queue->TryPush(NrInput(2, HeartbeatPacketType, NrDispatchLane::ServerIngress, NrPayload{}))
                            .Succeeded());
            NrQueueIngress ingress(*queue);
            NrIngressRegistry registry = CreateRegistryWith(ingress);
            const NrPacketParser parser = CreateParser();
            const NrPacketDispatchTable dispatchTable = CreateHeartbeatDispatchTable();
            NrInputFactory inputFactory(*manager);
            const std::vector<std::byte> packetBytes = MakePacketBytes(HeartbeatPacketType);
            NrRecvBuffer recvBuffer = CreateRecvBuffer(*manager, std::span(packetBytes));

            const NrRecvDispatchFlowResult result =
                DrainOnePacket(3, recvBuffer, parser, dispatchTable, inputFactory, registry);

            EXPECT_TRUE(result.status.Failed());
            EXPECT_EQ(result.status.ErrorCode(), NrErrorCode::QueueFull);
            EXPECT_EQ(result.parseStatus, NrPacketParseStatus::Complete);
            EXPECT_EQ(recvBuffer.ReadableBytes(), packetBytes.size());
            EXPECT_EQ(recvBuffer.ConsumedBytes(), 0u);
            EXPECT_EQ(queue->SizeApprox(), 2u);
        }

        TEST(NrRecvDispatchFlowTests, PayloadAllocationFailureDoesNotConsumeFrame)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = CreateFlowTestManager();
            ASSERT_NE(manager, nullptr);
            NrResult<NrPooledMemoryBlock> reservedPayloadBlockResult =
                manager->AcquireBlock(NrMemoryPoolRole::Payload64);
            ASSERT_TRUE(reservedPayloadBlockResult.Succeeded());
            NrPooledMemoryBlock reservedPayloadBlock = reservedPayloadBlockResult.TakeValue();
            std::unique_ptr<NrInputQueue> queue = CreateInputQueue(*manager);
            ASSERT_NE(queue, nullptr);
            NrQueueIngress ingress(*queue);
            NrIngressRegistry registry = CreateRegistryWith(ingress);
            const NrPacketParser parser = CreateParser();
            const NrPacketDispatchTable dispatchTable = CreateHeartbeatDispatchTable();
            NrInputFactory inputFactory(*manager);
            const std::array<std::byte, 1> payloadBytes = {std::byte{0x44}};
            const std::vector<std::byte> packetBytes =
                MakePacketBytes(HeartbeatPacketType, std::span(payloadBytes));
            NrRecvBuffer recvBuffer = CreateRecvBuffer(*manager, std::span(packetBytes));

            const NrRecvDispatchFlowResult result =
                DrainOnePacket(3, recvBuffer, parser, dispatchTable, inputFactory, registry);

            EXPECT_TRUE(result.status.Failed());
            EXPECT_EQ(result.status.ErrorCode(), NrErrorCode::PoolExhausted);
            EXPECT_EQ(result.parseStatus, NrPacketParseStatus::Complete);
            EXPECT_EQ(recvBuffer.ReadableBytes(), packetBytes.size());
            EXPECT_EQ(recvBuffer.ConsumedBytes(), 0u);
            EXPECT_EQ(queue->SizeApprox(), 0u);
            EXPECT_EQ(test::Stats(*manager, NrMemoryPoolRole::Payload64).inUse, 1u);
        }

        TEST(NrRecvDispatchFlowTests, PartialFrameDoesNotConsumeBytes)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = CreateFlowTestManager();
            ASSERT_NE(manager, nullptr);
            std::unique_ptr<NrInputQueue> queue = CreateInputQueue(*manager);
            ASSERT_NE(queue, nullptr);
            NrQueueIngress ingress(*queue);
            NrIngressRegistry registry = CreateRegistryWith(ingress);
            const NrPacketParser parser = CreateParser();
            const NrPacketDispatchTable dispatchTable = CreateHeartbeatDispatchTable();
            NrInputFactory inputFactory(*manager);
            const std::vector<std::byte> completePacket = MakePacketBytes(HeartbeatPacketType);
            const std::span<const std::byte> partialPacket(completePacket.data(), NrPacketHeaderLength - 1);
            NrRecvBuffer recvBuffer = CreateRecvBuffer(*manager, partialPacket);

            const NrRecvDispatchFlowResult result =
                DrainOnePacket(1, recvBuffer, parser, dispatchTable, inputFactory, registry);

            EXPECT_TRUE(result.status.Succeeded());
            EXPECT_EQ(result.parseStatus, NrPacketParseStatus::NeedMoreData);
            EXPECT_EQ(recvBuffer.ReadableBytes(), partialPacket.size());
            EXPECT_EQ(recvBuffer.ConsumedBytes(), 0u);
            EXPECT_EQ(queue->SizeApprox(), 0u);
        }
    } // namespace
} // namespace psnr::core
