#include "pch.h"

#include "NrConcurrency.h"

#include <type_traits>

namespace psnr::core
{

    namespace
    {
        TEST(NrConcurrencyTests, MutexIsNotCopyableOrMovable)
        {
            EXPECT_FALSE(std::is_copy_constructible_v<NrMutex>);
            EXPECT_FALSE(std::is_copy_assignable_v<NrMutex>);
            EXPECT_FALSE(std::is_move_constructible_v<NrMutex>);
            EXPECT_FALSE(std::is_move_assignable_v<NrMutex>);
        }

        TEST(NrConcurrencyTests, MutexTryLockAcquiresUnlockedLock)
        {
            NrMutex lock;

            EXPECT_TRUE(lock.TryLock());

            lock.Unlock();
        }

        TEST(NrConcurrencyTests, ScopedLockReleasesOnDestruction)
        {
            NrMutex lock;

            {
                NrScopedLock<NrMutex> scopedLock(lock);
            }

            EXPECT_TRUE(lock.TryLock());

            lock.Unlock();
        }

        TEST(NrConcurrencyTests, ScopedLockIsNotCopyableOrMovable)
        {
            EXPECT_FALSE(std::is_copy_constructible_v<NrScopedLock<NrMutex>>);
            EXPECT_FALSE(std::is_copy_assignable_v<NrScopedLock<NrMutex>>);
            EXPECT_FALSE(std::is_move_constructible_v<NrScopedLock<NrMutex>>);
            EXPECT_FALSE(std::is_move_assignable_v<NrScopedLock<NrMutex>>);
        }
    } // namespace
} // namespace psnr::core
