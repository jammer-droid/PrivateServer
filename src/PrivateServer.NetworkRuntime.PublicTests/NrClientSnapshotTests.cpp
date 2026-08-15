#include "pch.h"

#include <PrivateServer/NetworkRuntime/NrClientSnapshot.h>

#include <cstdint>
#include <type_traits>

namespace psnr::runtime
{
    namespace
    {
        TEST(NrClientSnapshotTests, PublicSnapshotIsCopyableOwningValue)
        {
            EXPECT_TRUE(std::is_nothrow_copy_constructible_v<NrClientSnapshot>);
            EXPECT_TRUE(std::is_nothrow_copy_assignable_v<NrClientSnapshot>);
            EXPECT_TRUE(std::is_nothrow_move_constructible_v<NrClientSnapshot>);
            EXPECT_TRUE(std::is_nothrow_move_assignable_v<NrClientSnapshot>);
            EXPECT_TRUE((std::is_same_v<std::underlying_type_t<NrClientLifecycleState>, std::uint8_t>));
        }

        TEST(NrClientSnapshotTests, DefaultSnapshotIsInvalidAndZeroInitialized)
        {
            const NrClientSnapshot snapshot;

            EXPECT_FALSE(snapshot.IsValid());
            EXPECT_EQ(snapshot.LifecycleState(), NrClientLifecycleState::Invalid);
            EXPECT_EQ(snapshot.PendingConnectIoCount(), 0u);
            EXPECT_EQ(snapshot.PendingRecvIoCount(), 0u);
            EXPECT_EQ(snapshot.PendingSendIoCount(), 0u);
            EXPECT_EQ(snapshot.PendingIoCount(), 0u);
            EXPECT_EQ(snapshot.EventQueueDepth(), 0u);
            EXPECT_EQ(snapshot.EventQueueHighWatermark(), 0u);
            EXPECT_EQ(snapshot.PendingSendQueueDepth(), 0u);
            EXPECT_EQ(snapshot.PendingSendQueueHighWatermark(), 0u);
        }

        TEST(NrClientSnapshotTests, DefaultSnapshotCopyIsIndependentOwningValue)
        {
            NrClientSnapshot source;
            const NrClientSnapshot copy = source;
            source = NrClientSnapshot{};

            EXPECT_FALSE(copy.IsValid());
            EXPECT_EQ(copy.LifecycleState(), NrClientLifecycleState::Invalid);
            EXPECT_EQ(copy.PendingIoCount(), 0u);
        }
    } // namespace
} // namespace psnr::runtime
