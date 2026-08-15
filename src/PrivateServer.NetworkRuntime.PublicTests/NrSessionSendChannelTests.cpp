#include "pch.h"

#include <PrivateServer/NetworkRuntime/NrSessionSendChannel.h>

#include <type_traits>
#include <utility>

namespace psnr::runtime
{
    namespace
    {
        TEST(NrSessionSendChannelTests, ChannelIsCopyableCheapHandle)
        {
            EXPECT_TRUE(std::is_copy_constructible_v<NrSessionSendChannel>);
            EXPECT_TRUE(std::is_copy_assignable_v<NrSessionSendChannel>);
            EXPECT_TRUE(std::is_move_constructible_v<NrSessionSendChannel>);
            EXPECT_TRUE(std::is_move_assignable_v<NrSessionSendChannel>);
        }

        TEST(NrSessionSendChannelTests, DefaultChannelIsInvalidAndClosed)
        {
            const NrSessionSendChannel channel;

            EXPECT_FALSE(channel.IsValid());
            EXPECT_FALSE(channel.IsOpen());
        }

        TEST(NrSessionSendChannelTests, DefaultChannelCanBeCopiedAndMoved)
        {
            const NrSessionSendChannel source;

            NrSessionSendChannel copy = source;
            NrSessionSendChannel moved = std::move(copy);

            EXPECT_FALSE(source.IsValid());
            EXPECT_FALSE(copy.IsValid());
            EXPECT_FALSE(moved.IsValid());
        }
    } // namespace
} // namespace psnr::runtime
