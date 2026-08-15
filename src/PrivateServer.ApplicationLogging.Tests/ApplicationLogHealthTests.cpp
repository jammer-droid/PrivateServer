#include "pch.h"

#include "ApplicationLogHealth.h"

namespace psnr::logging
{
    TEST(ApplicationLogHealthTests, DefaultsToStoppedHealthyAndEmpty)
    {
        const ApplicationLogHealth health{};

        EXPECT_FALSE(health.started);
        EXPECT_FALSE(health.fileSinkFailed);
        EXPECT_FALSE(health.consoleSinkFailed);
        EXPECT_EQ(health.attempted, 0);
        EXPECT_EQ(health.filtered, 0);
        EXPECT_EQ(health.enqueued, 0);
        EXPECT_EQ(health.consumed, 0);
        EXPECT_EQ(health.droppedQueueFull, 0);
        EXPECT_EQ(health.discardedAfterSinkFailure, 0);
        EXPECT_EQ(health.currentQueueDepth, 0);
        EXPECT_EQ(health.maximumQueueDepth, 0);
    }

    TEST(ApplicationLogHealthTests, PreservesIndependentSnapshotValues)
    {
        ApplicationLogHealth health{};
        health.started = true;
        health.fileSinkFailed = true;
        health.consoleSinkFailed = true;
        health.attempted = 100;
        health.filtered = 10;
        health.enqueued = 80;
        health.consumed = 70;
        health.droppedQueueFull = 10;
        health.discardedAfterSinkFailure = 10;
        health.currentQueueDepth = 4;
        health.maximumQueueDepth = 64;

        const ApplicationLogHealth snapshot = health;

        EXPECT_TRUE(snapshot.started);
        EXPECT_TRUE(snapshot.fileSinkFailed);
        EXPECT_TRUE(snapshot.consoleSinkFailed);
        EXPECT_EQ(snapshot.attempted, 100);
        EXPECT_EQ(snapshot.filtered, 10);
        EXPECT_EQ(snapshot.enqueued, 80);
        EXPECT_EQ(snapshot.consumed, 70);
        EXPECT_EQ(snapshot.droppedQueueFull, 10);
        EXPECT_EQ(snapshot.discardedAfterSinkFailure, 10);
        EXPECT_EQ(snapshot.currentQueueDepth, 4);
        EXPECT_EQ(snapshot.maximumQueueDepth, 64);
    }
} // namespace psnr::logging
