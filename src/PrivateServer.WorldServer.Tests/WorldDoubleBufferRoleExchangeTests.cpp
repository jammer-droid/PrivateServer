#include "pch.h"

#include "WorldDoubleBufferRoleExchange.h"

#include <chrono>
#include <future>

namespace psnr::world::tests
{
    namespace
    {
        constexpr std::chrono::milliseconds TestTimeout{500};
        constexpr std::chrono::milliseconds MustRemainBlockedWindow{20};
    } // namespace

    TEST(WorldDoubleBufferRoleExchangeTests, AlternatesWriteAndReadSlotsAtEachSwap)
    {
        WorldDoubleBufferRoleExchange exchange;

        WorldResult<WorldDoubleBufferWriteClaim, WorldDoubleBufferRoleExchangeError> firstWriteResult =
            exchange.WaitAcquireWrite(0, TestTimeout);
        ASSERT_TRUE(firstWriteResult.Succeeded());
        WorldDoubleBufferWriteClaim firstWrite = firstWriteResult.TakeValue();
        EXPECT_EQ(firstWrite.slotIndex, 0u);
        ASSERT_TRUE(exchange.ReleaseWrite(firstWrite).Succeeded());

        ASSERT_TRUE(exchange.WaitSwap(1, TestTimeout).Succeeded());

        WorldResult<WorldDoubleBufferReadClaim, WorldDoubleBufferRoleExchangeError> firstReadResult =
            exchange.WaitAcquireRead(1, TestTimeout);
        ASSERT_TRUE(firstReadResult.Succeeded());
        WorldDoubleBufferReadClaim firstRead = firstReadResult.TakeValue();
        EXPECT_EQ(firstRead.slotIndex, 0u);

        WorldResult<WorldDoubleBufferWriteClaim, WorldDoubleBufferRoleExchangeError> secondWriteResult =
            exchange.WaitAcquireWrite(0, TestTimeout);
        ASSERT_TRUE(secondWriteResult.Succeeded());
        WorldDoubleBufferWriteClaim secondWrite = secondWriteResult.TakeValue();
        EXPECT_EQ(secondWrite.slotIndex, 1u);
        EXPECT_NE(firstRead.slotIndex, secondWrite.slotIndex);

        ASSERT_TRUE(exchange.ReleaseRead(firstRead).Succeeded());
        ASSERT_TRUE(exchange.ReleaseWrite(secondWrite).Succeeded());
        ASSERT_TRUE(exchange.WaitSwap(2, TestTimeout).Succeeded());

        WorldResult<WorldDoubleBufferReadClaim, WorldDoubleBufferRoleExchangeError> secondReadResult =
            exchange.WaitAcquireRead(2, TestTimeout);
        ASSERT_TRUE(secondReadResult.Succeeded());
        WorldDoubleBufferReadClaim secondRead = secondReadResult.TakeValue();
        EXPECT_EQ(secondRead.slotIndex, 1u);
        EXPECT_NE(firstRead.generation, secondRead.generation);
        EXPECT_TRUE(exchange.ReleaseRead(secondRead).Succeeded());
    }

    TEST(WorldDoubleBufferRoleExchangeTests, SwapWaitsForActiveWriteClaim)
    {
        WorldDoubleBufferRoleExchange exchange;
        WorldResult<WorldDoubleBufferWriteClaim, WorldDoubleBufferRoleExchangeError> writeResult =
            exchange.WaitAcquireWrite(0, TestTimeout);
        ASSERT_TRUE(writeResult.Succeeded());
        WorldDoubleBufferWriteClaim writeClaim = writeResult.TakeValue();

        std::future<WorldResult<void, WorldDoubleBufferRoleExchangeError>> swapResult =
            std::async(std::launch::async, [&exchange]() { return exchange.WaitSwap(1, TestTimeout); });

        EXPECT_EQ(swapResult.wait_for(MustRemainBlockedWindow), std::future_status::timeout);
        ASSERT_TRUE(exchange.ReleaseWrite(writeClaim).Succeeded());
        const WorldResult<void, WorldDoubleBufferRoleExchangeError> completedSwap = swapResult.get();
        EXPECT_TRUE(completedSwap.Succeeded());
    }

    TEST(WorldDoubleBufferRoleExchangeTests, SwapWaitsForActiveReadClaim)
    {
        WorldDoubleBufferRoleExchange exchange;
        ASSERT_TRUE(exchange.WaitSwap(1, TestTimeout).Succeeded());

        WorldResult<WorldDoubleBufferReadClaim, WorldDoubleBufferRoleExchangeError> readResult =
            exchange.WaitAcquireRead(1, TestTimeout);
        ASSERT_TRUE(readResult.Succeeded());
        WorldDoubleBufferReadClaim readClaim = readResult.TakeValue();

        std::future<WorldResult<void, WorldDoubleBufferRoleExchangeError>> swapResult =
            std::async(std::launch::async, [&exchange]() { return exchange.WaitSwap(2, TestTimeout); });

        EXPECT_EQ(swapResult.wait_for(MustRemainBlockedWindow), std::future_status::timeout);
        ASSERT_TRUE(exchange.ReleaseRead(readClaim).Succeeded());
        const WorldResult<void, WorldDoubleBufferRoleExchangeError> completedSwap = swapResult.get();
        EXPECT_TRUE(completedSwap.Succeeded());
    }

    TEST(WorldDoubleBufferRoleExchangeTests, DoesNotOverwritePublishedUnreadBatch)
    {
        WorldDoubleBufferRoleExchange exchange;
        ASSERT_TRUE(exchange.WaitSwap(1, TestTimeout).Succeeded());

        std::future<WorldResult<void, WorldDoubleBufferRoleExchangeError>> swapResult =
            std::async(std::launch::async, [&exchange]() { return exchange.WaitSwap(2, TestTimeout); });

        EXPECT_EQ(swapResult.wait_for(MustRemainBlockedWindow), std::future_status::timeout);

        WorldResult<WorldDoubleBufferReadClaim, WorldDoubleBufferRoleExchangeError> readResult =
            exchange.WaitAcquireRead(1, TestTimeout);
        ASSERT_TRUE(readResult.Succeeded());
        WorldDoubleBufferReadClaim readClaim = readResult.TakeValue();
        ASSERT_TRUE(exchange.ReleaseRead(readClaim).Succeeded());
        const WorldResult<void, WorldDoubleBufferRoleExchangeError> completedSwap = swapResult.get();
        EXPECT_TRUE(completedSwap.Succeeded());
    }

    TEST(WorldDoubleBufferRoleExchangeTests, RejectsStaleClaimAfterGenerationAdvances)
    {
        WorldDoubleBufferRoleExchange exchange;
        WorldResult<WorldDoubleBufferWriteClaim, WorldDoubleBufferRoleExchangeError> staleClaimResult =
            exchange.WaitAcquireWrite(0, TestTimeout);
        ASSERT_TRUE(staleClaimResult.Succeeded());
        WorldDoubleBufferWriteClaim staleClaim = staleClaimResult.TakeValue();
        ASSERT_TRUE(exchange.ReleaseWrite(staleClaim).Succeeded());
        ASSERT_TRUE(exchange.WaitSwap(1, TestTimeout).Succeeded());

        WorldResult<WorldDoubleBufferWriteClaim, WorldDoubleBufferRoleExchangeError> currentClaimResult =
            exchange.WaitAcquireWrite(0, TestTimeout);
        ASSERT_TRUE(currentClaimResult.Succeeded());
        WorldDoubleBufferWriteClaim currentClaim = currentClaimResult.TakeValue();
        const WorldResult<void, WorldDoubleBufferRoleExchangeError> staleRelease = exchange.ReleaseWrite(staleClaim);
        ASSERT_TRUE(staleRelease.Failed());
        EXPECT_EQ(staleRelease.Error(), WorldDoubleBufferRoleExchangeError::InvalidState);
        EXPECT_TRUE(exchange.ReleaseWrite(currentClaim).Succeeded());
    }

    TEST(WorldDoubleBufferRoleExchangeTests, CloseWakesWaitingReader)
    {
        WorldDoubleBufferRoleExchange exchange;
        std::future<WorldResult<WorldDoubleBufferReadClaim, WorldDoubleBufferRoleExchangeError>> readResult =
            std::async(std::launch::async, [&exchange]() { return exchange.WaitAcquireRead(1, TestTimeout); });

        exchange.Close();

        const WorldResult<WorldDoubleBufferReadClaim, WorldDoubleBufferRoleExchangeError> completedRead =
            readResult.get();
        ASSERT_TRUE(completedRead.Failed());
        EXPECT_EQ(completedRead.Error(), WorldDoubleBufferRoleExchangeError::Closed);
    }

    TEST(WorldDoubleBufferRoleExchangeTests, CloseRejectsPublishedReadBatchAcquisition)
    {
        WorldDoubleBufferRoleExchange exchange;
        ASSERT_TRUE(exchange.WaitSwap(1, TestTimeout).Succeeded());

        exchange.Close();

        const WorldResult<WorldDoubleBufferReadClaim, WorldDoubleBufferRoleExchangeError> readResult =
            exchange.WaitAcquireRead(1, TestTimeout);
        ASSERT_TRUE(readResult.Failed());
        EXPECT_EQ(readResult.Error(), WorldDoubleBufferRoleExchangeError::Closed);
    }

    TEST(WorldDoubleBufferRoleExchangeTests, CloseWakesWaitingSwapAndAllowsActiveReadRelease)
    {
        WorldDoubleBufferRoleExchange exchange;
        ASSERT_TRUE(exchange.WaitSwap(1, TestTimeout).Succeeded());
        WorldResult<WorldDoubleBufferReadClaim, WorldDoubleBufferRoleExchangeError> readResult =
            exchange.WaitAcquireRead(1, TestTimeout);
        ASSERT_TRUE(readResult.Succeeded());
        WorldDoubleBufferReadClaim readClaim = readResult.TakeValue();

        std::future<WorldResult<void, WorldDoubleBufferRoleExchangeError>> swapResult =
            std::async(std::launch::async, [&exchange]() { return exchange.WaitSwap(2, TestTimeout); });
        ASSERT_EQ(swapResult.wait_for(MustRemainBlockedWindow), std::future_status::timeout);

        exchange.Close();

        const WorldResult<void, WorldDoubleBufferRoleExchangeError> completedSwap = swapResult.get();
        ASSERT_TRUE(completedSwap.Failed());
        EXPECT_EQ(completedSwap.Error(), WorldDoubleBufferRoleExchangeError::Closed);
        EXPECT_TRUE(exchange.ReleaseRead(readClaim).Succeeded());
    }

    TEST(WorldDoubleBufferRoleExchangeTests, PendingSwapBlocksNewWriteClaimUntilRoleChange)
    {
        WorldDoubleBufferRoleExchange exchange;
        WorldResult<WorldDoubleBufferWriteClaim, WorldDoubleBufferRoleExchangeError> firstWriteResult =
            exchange.WaitAcquireWrite(0, TestTimeout);
        ASSERT_TRUE(firstWriteResult.Succeeded());
        WorldDoubleBufferWriteClaim firstWrite = firstWriteResult.TakeValue();

        std::future<WorldResult<void, WorldDoubleBufferRoleExchangeError>> swapResult =
            std::async(std::launch::async, [&exchange]() { return exchange.WaitSwap(1, TestTimeout); });
        const std::uint64_t nextGeneration = firstWrite.generation + 1;
        std::future<WorldResult<WorldDoubleBufferWriteClaim, WorldDoubleBufferRoleExchangeError>> nextWriteResult =
            std::async(std::launch::async, [&exchange, nextGeneration]()
                       { return exchange.WaitAcquireWrite(nextGeneration, TestTimeout); });

        EXPECT_EQ(nextWriteResult.wait_for(MustRemainBlockedWindow), std::future_status::timeout);
        ASSERT_TRUE(exchange.ReleaseWrite(firstWrite).Succeeded());
        ASSERT_TRUE(swapResult.get().Succeeded());

        WorldResult<WorldDoubleBufferWriteClaim, WorldDoubleBufferRoleExchangeError> nextWrite = nextWriteResult.get();
        ASSERT_TRUE(nextWrite.Succeeded());
        WorldDoubleBufferWriteClaim nextWriteClaim = nextWrite.TakeValue();
        EXPECT_EQ(nextWriteClaim.generation, nextGeneration);
        EXPECT_TRUE(exchange.ReleaseWrite(nextWriteClaim).Succeeded());
    }
} // namespace psnr::world::tests
