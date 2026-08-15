#include "pch.h"

#include "NrClientConnectIoContext.h"
#include "NrClientIoContextOperations.h"
#include "NrMemoryPoolTestUtils.h"
#include "NrPayloadRef.h"
#include "NrSocketAddressWin32.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>

namespace psnr::runtime::internal
{
    namespace
    {
        [[nodiscard]] psnr::core::NrPayloadRef CreatePayload(psnr::core::NrMemoryPoolManager& manager,
                                                             const std::span<const std::byte> bytes)
        {
            psnr::core::NrResult<psnr::core::NrPayloadRef> result =
                psnr::core::NrPayloadRefFactory::CreatePayloadRefFrom(manager, bytes);
            EXPECT_TRUE(result.Succeeded());
            return result.Failed() ? psnr::core::NrPayloadRef{} : result.TakeValue();
        }

        TEST(NrClientIoContextOperationsTests, ConnectContextOwnsGenerationAddressAndStableOverlapped)
        {
            EXPECT_FALSE(std::is_copy_constructible_v<NrClientConnectIoContext>);
            EXPECT_FALSE(std::is_move_constructible_v<NrClientConnectIoContext>);

            NrSocketAddressWin32 remoteAddress;
            remoteAddress.address.sin_family = AF_INET;
            remoteAddress.address.sin_port = htons(7777);
            remoteAddress.address.sin_addr.S_un.S_addr = htonl(0x7F000001u);

            NrClientConnectIoContext context(17, remoteAddress);
            OVERLAPPED* const overlapped = context.Overlapped();
            remoteAddress.address.sin_port = htons(9999);

            ASSERT_NE(overlapped, nullptr);
            EXPECT_EQ(context.Type(), NrIoOperationType::Connect);
            EXPECT_EQ(context.AttemptGeneration(), 17u);
            EXPECT_EQ(NrClientConnectIoContext::FromOverlapped(overlapped), &context);
            EXPECT_EQ(context.Overlapped(), overlapped);
            ASSERT_NE(context.RemoteAddress(), nullptr);
            EXPECT_EQ(context.RemoteAddress()->sa_family, AF_INET);
            const sockaddr_in* storedAddress = reinterpret_cast<const sockaddr_in*>(context.RemoteAddress());
            EXPECT_EQ(storedAddress->sin_port, htons(7777));
            EXPECT_EQ(context.RemoteAddressLength(), sizeof(sockaddr_in));
        }

        TEST(NrClientIoContextOperationsTests, RecvPreparationPreservesAddressAndRejectsResetWhilePending)
        {
            std::array<std::byte, 4> firstBuffer{};
            std::array<std::byte, 8> secondBuffer{};
            NrRecvIoContext context(1, std::span(firstBuffer));
            NrClientIoContextOperations operations;
            OVERLAPPED* const overlapped = context.header.Overlapped();

            ASSERT_TRUE(operations.PrepareRecv(context, 7, std::span(secondBuffer)).Succeeded());
            EXPECT_TRUE(operations.HasPendingRecv());
            EXPECT_EQ(context.header.Overlapped(), overlapped);
            EXPECT_EQ(context.header.Type(), NrIoOperationType::Recv);
            EXPECT_EQ(NrClientIoContextOperations::AttemptGeneration(context), 7u);
            EXPECT_EQ(context.writableLength, secondBuffer.size());
            EXPECT_EQ(context.wsabuf.buf, reinterpret_cast<char*>(secondBuffer.data()));
            EXPECT_EQ(context.wsabuf.len, secondBuffer.size());

            EXPECT_EQ(operations.PrepareRecv(context, 8, std::span(firstBuffer)).ErrorCode(),
                      psnr::core::NrErrorCode::InvalidState);
            EXPECT_EQ(NrClientIoContextOperations::AttemptGeneration(context), 7u);
            EXPECT_EQ(context.wsabuf.buf, reinterpret_cast<char*>(secondBuffer.data()));

            ASSERT_TRUE(operations.ReleasePendingRecv(context).Succeeded());
            EXPECT_FALSE(operations.HasPendingRecv());
            ASSERT_TRUE(operations.PrepareRecv(context, 8, std::span(firstBuffer)).Succeeded());
            EXPECT_EQ(context.header.Overlapped(), overlapped);
            EXPECT_EQ(NrClientIoContextOperations::AttemptGeneration(context), 8u);
            EXPECT_EQ(context.wsabuf.buf, reinterpret_cast<char*>(firstBuffer.data()));
            EXPECT_TRUE(operations.ReleasePendingRecv(context).Succeeded());
        }

        TEST(NrClientIoContextOperationsTests, SendRepostPreservesPayloadOffsetAndRejectsResetWhilePending)
        {
            psnr::core::NrMemoryPoolManagerConfig config = psnr::core::test::MakeDefaultMemoryPoolManagerConfig();
            ASSERT_TRUE(psnr::core::test::SetPoolConfig(config, psnr::core::NrMemoryPoolRole::Payload64,
                                                        psnr::core::test::MakePoolConfig(64, 2)));
            ASSERT_TRUE(psnr::core::test::SetPoolConfig(config, psnr::core::NrMemoryPoolRole::PayloadRefControl,
                                                        psnr::core::test::MakePoolConfig(64, 2)));
            std::unique_ptr<psnr::core::NrMemoryPoolManager> manager =
                psnr::core::test::CreateMemoryPoolManager(config);
            ASSERT_NE(manager, nullptr);
            const std::array<std::byte, 4> firstBytes = {
                std::byte{0x10},
                std::byte{0x20},
                std::byte{0x30},
                std::byte{0x40},
            };
            const std::array<std::byte, 2> secondBytes = {
                std::byte{0x50},
                std::byte{0x60},
            };
            psnr::core::NrPayloadRef firstPayload = CreatePayload(*manager, std::span(firstBytes));
            psnr::core::NrPayloadRef secondPayload = CreatePayload(*manager, std::span(secondBytes));
            ASSERT_FALSE(firstPayload.IsEmpty());
            ASSERT_FALSE(secondPayload.IsEmpty());

            NrSendIoContext context(0, psnr::core::NrPayloadRef{});
            NrClientIoContextOperations operations;
            OVERLAPPED* const overlapped = context.header.Overlapped();

            ASSERT_TRUE(operations.PrepareSendContext(context, 11, std::move(firstPayload)).Succeeded());
            EXPECT_TRUE(firstPayload.IsEmpty());
            EXPECT_TRUE(operations.HasPendingSend());
            EXPECT_EQ(context.header.Overlapped(), overlapped);
            EXPECT_EQ(NrClientIoContextOperations::AttemptGeneration(context), 11u);

            EXPECT_EQ(operations.PrepareSendContext(context, 12, std::move(secondPayload)).ErrorCode(),
                      psnr::core::NrErrorCode::InvalidState);
            EXPECT_FALSE(secondPayload.IsEmpty());
            EXPECT_EQ(NrClientIoContextOperations::AttemptGeneration(context), 11u);

            NrClientSendCompletionResult result = NrClientSendCompletionResult::None;
            ASSERT_TRUE(operations.CompleteSendContext(context, psnr::core::NrStatus::Success(), 2,
                                                       result).Succeeded());
            EXPECT_EQ(result, NrClientSendCompletionResult::NeedsRepost);
            EXPECT_FALSE(operations.HasPendingSend());
            const std::byte* const payloadAddress = context.payloadRef.Bytes().data();
            const std::uint32_t bytesSent = context.bytesSent;
            char* const remainingAddress = context.wsabuf.buf;
            const ULONG remainingLength = context.wsabuf.len;

            EXPECT_EQ(operations.PrepareSendContext(context, 12, std::move(secondPayload)).ErrorCode(),
                      psnr::core::NrErrorCode::InvalidState);
            EXPECT_FALSE(secondPayload.IsEmpty());
            EXPECT_EQ(context.payloadRef.Bytes().data(), payloadAddress);
            EXPECT_EQ(context.bytesSent, bytesSent);

            ASSERT_TRUE(operations.PrepareSendContextForRepost(context).Succeeded());
            EXPECT_EQ(context.header.Overlapped(), overlapped);
            EXPECT_EQ(context.payloadRef.Bytes().data(), payloadAddress);
            EXPECT_EQ(context.bytesSent, bytesSent);
            EXPECT_EQ(context.wsabuf.buf, remainingAddress);
            EXPECT_EQ(context.wsabuf.len, remainingLength);
            EXPECT_EQ(NrClientIoContextOperations::AttemptGeneration(context), 11u);

            EXPECT_EQ(operations.PrepareSendContextForRepost(context).ErrorCode(),
                      psnr::core::NrErrorCode::InvalidState);

            ASSERT_TRUE(operations.CompleteSendContext(context, psnr::core::NrStatus::Success(), 2,
                                                       result).Succeeded());
            EXPECT_EQ(result, NrClientSendCompletionResult::Completed);
            EXPECT_FALSE(operations.HasPendingSend());
            EXPECT_TRUE(context.payloadRef.IsEmpty());
            EXPECT_EQ(context.payloadLength, 0u);
            EXPECT_EQ(context.bytesSent, 0u);
            EXPECT_EQ(context.wsabuf.buf, nullptr);
            EXPECT_EQ(context.wsabuf.len, 0u);

            EXPECT_EQ(operations.CompleteSendContext(context, psnr::core::NrStatus::Success(), 1,
                                                     result).ErrorCode(),
                      psnr::core::NrErrorCode::InvalidState);
            EXPECT_EQ(result, NrClientSendCompletionResult::None);
        }

        TEST(NrClientIoContextOperationsTests, FailedAndZeroByteSendCompletionsReturnContextToIdle)
        {
            psnr::core::NrMemoryPoolManagerConfig config = psnr::core::test::MakeDefaultMemoryPoolManagerConfig();
            ASSERT_TRUE(psnr::core::test::SetPoolConfig(config, psnr::core::NrMemoryPoolRole::Payload64,
                                                        psnr::core::test::MakePoolConfig(64, 2)));
            ASSERT_TRUE(psnr::core::test::SetPoolConfig(config, psnr::core::NrMemoryPoolRole::PayloadRefControl,
                                                        psnr::core::test::MakePoolConfig(64, 2)));
            std::unique_ptr<psnr::core::NrMemoryPoolManager> manager =
                psnr::core::test::CreateMemoryPoolManager(config);
            ASSERT_NE(manager, nullptr);
            const std::array<std::byte, 2> firstBytes = {std::byte{0x10}, std::byte{0x20}};
            const std::array<std::byte, 2> secondBytes = {std::byte{0x30}, std::byte{0x40}};
            psnr::core::NrPayloadRef firstPayload = CreatePayload(*manager, std::span(firstBytes));
            psnr::core::NrPayloadRef secondPayload = CreatePayload(*manager, std::span(secondBytes));
            ASSERT_FALSE(firstPayload.IsEmpty());
            ASSERT_FALSE(secondPayload.IsEmpty());

            NrSendIoContext context(0, psnr::core::NrPayloadRef{});
            NrClientIoContextOperations operations;
            NrClientSendCompletionResult result = NrClientSendCompletionResult::Completed;

            ASSERT_TRUE(operations.PrepareSendContext(context, 13, std::move(firstPayload)).Succeeded());
            const psnr::core::NrStatus failedCompletion =
                psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::IoFailed, 10054);
            const psnr::core::NrStatus failedStatus =
                operations.CompleteSendContext(context, failedCompletion, 0, result);
            EXPECT_EQ(failedStatus.ErrorCode(), failedCompletion.ErrorCode());
            EXPECT_EQ(failedStatus.NativeErrorCode(), failedCompletion.NativeErrorCode());
            EXPECT_EQ(result, NrClientSendCompletionResult::None);
            EXPECT_FALSE(operations.HasPendingSend());
            EXPECT_TRUE(context.payloadRef.IsEmpty());

            ASSERT_TRUE(operations.PrepareSendContext(context, 13, std::move(secondPayload)).Succeeded());
            const psnr::core::NrStatus zeroByteStatus =
                operations.CompleteSendContext(context, psnr::core::NrStatus::Success(), 0, result);
            EXPECT_EQ(zeroByteStatus.ErrorCode(), psnr::core::NrErrorCode::IoFailed);
            EXPECT_EQ(result, NrClientSendCompletionResult::None);
            EXPECT_FALSE(operations.HasPendingSend());
            EXPECT_TRUE(context.payloadRef.IsEmpty());
        }
    } // namespace
} // namespace psnr::runtime::internal
