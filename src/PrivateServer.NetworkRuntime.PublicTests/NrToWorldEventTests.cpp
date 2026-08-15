#include "pch.h"

#include <PrivateServer/NetworkRuntime/NrSessionCloseRequestReason.h>
#include <PrivateServer/NetworkRuntime/NrToWorldEvent.h>

#include <type_traits>
#include <utility>

namespace psnr::runtime
{
    using psnr::core::NrErrorCode;

    namespace
    {
        constexpr NrPacketType MoveInputPacketType{1};

        TEST(NrToWorldEventTests, PublicEventIsPointerSizedMoveOnlyOwner)
        {
            EXPECT_FALSE(std::is_copy_constructible_v<NrToWorldEvent>);
            EXPECT_FALSE(std::is_copy_assignable_v<NrToWorldEvent>);
            EXPECT_TRUE(std::is_nothrow_move_constructible_v<NrToWorldEvent>);
            EXPECT_TRUE(std::is_nothrow_move_assignable_v<NrToWorldEvent>);
            EXPECT_EQ(sizeof(NrToWorldEvent), sizeof(void*));
        }

        TEST(NrToWorldEventTests, PublicValuesUseFixedWidthAndDistinctReasonTypes)
        {
            EXPECT_TRUE((std::is_same_v<std::underlying_type_t<NrToWorldEventKind>, std::uint8_t>));
            EXPECT_TRUE((std::is_same_v<std::underlying_type_t<NrSessionCloseRequestReason>, std::uint8_t>));
            EXPECT_TRUE((std::is_same_v<std::underlying_type_t<NrSessionEndReason>, std::uint8_t>));
            EXPECT_FALSE((std::is_same_v<NrSessionCloseRequestReason, NrSessionEndReason>));

            const NrByteView emptyView;
            EXPECT_EQ(emptyView.data, nullptr);
            EXPECT_EQ(emptyView.size, 0u);
        }

        TEST(NrToWorldEventTests, DefaultEventIsInvalidAndHasNoIdentity)
        {
            const NrToWorldEvent event;

            EXPECT_FALSE(event.IsValid());
            EXPECT_EQ(event.Kind(), NrToWorldEventKind::None);
            EXPECT_EQ(event.SessionKey(), 0u);
        }

        TEST(NrToWorldEventTests, DefaultEventCanBeMovedAndMovedFromRemainsInvalid)
        {
            NrToWorldEvent source;
            NrToWorldEvent moved = std::move(source);
            NrToWorldEvent assigned;
            assigned = std::move(moved);

            EXPECT_FALSE(source.IsValid());
            EXPECT_FALSE(moved.IsValid());
            EXPECT_FALSE(assigned.IsValid());
        }

        TEST(NrToWorldEventTests, WrongKindAccessorsRejectWithoutChangingOutputs)
        {
            static constexpr std::byte PayloadSentinel[] = {std::byte{0x7f}};

            const NrToWorldEvent event;
            NrSessionSendChannel channel;
            NrPacketType packetType = MoveInputPacketType;
            NrByteView payload{PayloadSentinel, 1};
            NrSessionEndReason endReason = NrSessionEndReason::RemoteClosed;

            EXPECT_EQ(event.GetSendChannel(&channel).ErrorCode(), NrErrorCode::InvalidState);
            EXPECT_EQ(event.GetPacketType(&packetType).ErrorCode(), NrErrorCode::InvalidState);
            EXPECT_EQ(event.GetPayload(&payload).ErrorCode(), NrErrorCode::InvalidState);
            EXPECT_EQ(event.GetEndReason(endReason).ErrorCode(), NrErrorCode::InvalidState);

            EXPECT_FALSE(channel.IsValid());
            EXPECT_EQ(packetType, MoveInputPacketType);
            EXPECT_EQ(payload.data, PayloadSentinel);
            EXPECT_EQ(payload.size, 1u);
            EXPECT_EQ(endReason, NrSessionEndReason::RemoteClosed);
        }

        TEST(NrToWorldEventTests, OutputAccessorsRejectNull)
        {
            const NrToWorldEvent event;

            EXPECT_EQ(event.GetSendChannel(nullptr).ErrorCode(), NrErrorCode::InvalidArgument);
            EXPECT_EQ(event.GetPacketType(nullptr).ErrorCode(), NrErrorCode::InvalidArgument);
            EXPECT_EQ(event.GetPayload(nullptr).ErrorCode(), NrErrorCode::InvalidArgument);
        }
    } // namespace
} // namespace psnr::runtime
