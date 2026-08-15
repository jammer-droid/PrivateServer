#include "pch.h"

#include <PrivateServer/NetworkRuntime/NrClientEvent.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace psnr::runtime
{
    using psnr::core::NrErrorCode;
    using psnr::core::NrStatus;

    namespace
    {
        TEST(NrClientEventTests, PublicEventIsMoveOnlyOwningValue)
        {
            EXPECT_FALSE(std::is_copy_constructible_v<NrClientEvent>);
            EXPECT_FALSE(std::is_copy_assignable_v<NrClientEvent>);
            EXPECT_TRUE(std::is_nothrow_move_constructible_v<NrClientEvent>);
            EXPECT_TRUE(std::is_nothrow_move_assignable_v<NrClientEvent>);
            EXPECT_TRUE((std::is_same_v<std::underlying_type_t<NrClientEventKind>, std::uint8_t>));
            EXPECT_TRUE((std::is_same_v<std::underlying_type_t<NrClientDisconnectReason>, std::uint8_t>));
        }

        TEST(NrClientEventTests, DefaultEventIsInvalidAndAccessorsPreserveOutput)
        {
            const NrClientEvent event;
            EXPECT_FALSE(event.IsValid());
            EXPECT_EQ(event.Kind(), NrClientEventKind::None);

            NrPacketType packetType{77};
            const std::byte payloadByte{0x2A};
            NrByteView payload{&payloadByte, 1};
            NrStatus transportStatus = NrStatus::Failure(NrErrorCode::IoFailed, 91);
            NrClientDisconnectReason reason = NrClientDisconnectReason::RemoteClosed;

            EXPECT_EQ(event.GetPacketType(&packetType).ErrorCode(), NrErrorCode::InvalidState);
            EXPECT_EQ(event.GetPayload(&payload).ErrorCode(), NrErrorCode::InvalidState);
            EXPECT_EQ(event.GetTransportStatus(&transportStatus).ErrorCode(), NrErrorCode::InvalidState);
            EXPECT_EQ(event.GetDisconnectReason(&reason).ErrorCode(), NrErrorCode::InvalidState);

            EXPECT_EQ(packetType.value, 77u);
            EXPECT_EQ(payload.data, &payloadByte);
            EXPECT_EQ(payload.size, 1u);
            EXPECT_EQ(transportStatus.ErrorCode(), NrErrorCode::IoFailed);
            EXPECT_EQ(transportStatus.NativeErrorCode(), 91u);
            EXPECT_EQ(reason, NrClientDisconnectReason::RemoteClosed);
        }

        TEST(NrClientEventTests, NullOutputsAreRejectedAtPublicBoundary)
        {
            const NrClientEvent event;

            EXPECT_EQ(event.GetPacketType(nullptr).ErrorCode(), NrErrorCode::InvalidArgument);
            EXPECT_EQ(event.GetPayload(nullptr).ErrorCode(), NrErrorCode::InvalidArgument);
            EXPECT_EQ(event.GetTransportStatus(nullptr).ErrorCode(), NrErrorCode::InvalidArgument);
            EXPECT_EQ(event.GetDisconnectReason(nullptr).ErrorCode(), NrErrorCode::InvalidArgument);
        }

        TEST(NrClientEventTests, MovingDefaultEventKeepsBothObjectsInvalid)
        {
            NrClientEvent source;
            NrClientEvent target(std::move(source));

            EXPECT_FALSE(source.IsValid());
            EXPECT_FALSE(target.IsValid());
            EXPECT_EQ(source.Kind(), NrClientEventKind::None);
            EXPECT_EQ(target.Kind(), NrClientEventKind::None);
        }
    } // namespace
} // namespace psnr::runtime
