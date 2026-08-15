#include "pch.h"

#include "WorldServerHostChildControlWorker.h"
#include "WorldServerHostStopSignal.h"

#define NOMINMAX
#include <Windows.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace psnr::world::tests
{
    namespace
    {
        class ChildControlWorkerHarness final
        {
        public:
            ~ChildControlWorkerHarness()
            {
                control_.reset();
                Close(commandWriteHandle_);
                Close(eventReadHandle_);
            }

            ChildControlWorkerHarness(const ChildControlWorkerHarness&) = delete;
            ChildControlWorkerHarness& operator=(const ChildControlWorkerHarness&) = delete;

            ChildControlWorkerHarness() noexcept = default;

            [[nodiscard]] bool Initialize()
            {
                HANDLE commandReadHandle = nullptr;
                if (CreatePipe(&commandReadHandle, &commandWriteHandle_, nullptr, 0) == FALSE)
                {
                    return false;
                }

                HANDLE eventWriteHandle = nullptr;
                if (CreatePipe(&eventReadHandle_, &eventWriteHandle, nullptr, 0) == FALSE)
                {
                    Close(commandReadHandle);
                    return false;
                }

                WorldResult<std::unique_ptr<host::WorldServerHostChildControl>,
                            host::WorldServerHostChildControlFailure>
                    controlResult =
                        host::WorldServerHostChildControl::Create("run-77",
                                                                  reinterpret_cast<std::uintptr_t>(commandReadHandle),
                                                                  reinterpret_cast<std::uintptr_t>(eventWriteHandle));
                if (controlResult.Failed())
                {
                    Close(commandReadHandle);
                    Close(eventWriteHandle);
                    return false;
                }

                control_ = controlResult.TakeValue();
                return true;
            }

            [[nodiscard]] host::WorldServerHostChildControl& Control() const noexcept
            {
                return *control_;
            }

            [[nodiscard]] bool WriteStop(const std::uint64_t sequence) const
            {
                return WriteCommand("stop", sequence);
            }

            [[nodiscard]] bool WriteCommand(const std::string_view type, const std::uint64_t sequence) const
            {
                const std::string command =
                    R"({"schema":"psnr.network_runtime.benchmark.control","version":1,"type":")" + std::string{type} +
                    R"(","runId":"run-77","sequence":)" + std::to_string(sequence) + "}\n";
                DWORD writtenBytes = 0;
                return WriteFile(commandWriteHandle_, command.data(), static_cast<DWORD>(command.size()), &writtenBytes,
                                 nullptr) != FALSE &&
                       writtenBytes == static_cast<DWORD>(command.size());
            }

            [[nodiscard]] bool ReadEvent(std::string* const outEvent) const
            {
                if (outEvent == nullptr)
                {
                    return false;
                }

                std::string event;
                while (true)
                {
                    char byte = '\0';
                    DWORD readBytes = 0;
                    if (ReadFile(eventReadHandle_, &byte, 1, &readBytes, nullptr) == FALSE || readBytes != 1)
                    {
                        return false;
                    }
                    if (byte == '\n')
                    {
                        *outEvent = std::move(event);
                        return true;
                    }
                    event.push_back(byte);
                }
            }

            void CloseCommandWriter() noexcept
            {
                Close(commandWriteHandle_);
            }

        private:
            static void Close(HANDLE& handle) noexcept
            {
                if (handle != nullptr && handle != INVALID_HANDLE_VALUE)
                {
                    static_cast<void>(CloseHandle(handle));
                    handle = nullptr;
                }
            }

            std::unique_ptr<host::WorldServerHostChildControl> control_;
            HANDLE commandWriteHandle_ = nullptr;
            HANDLE eventReadHandle_ = nullptr;
        };

        [[nodiscard]] bool WaitForResult(host::WorldServerHostChildControlWorker& worker,
                                         const host::WorldServerHostChildControlWorkerResult expected) noexcept
        {
            for (std::uint32_t attempt = 0; attempt < 1000; ++attempt)
            {
                if (worker.Result() == expected)
                {
                    return true;
                }
                Sleep(1);
            }
            return false;
        }
    } // namespace

    TEST(WorldServerHostChildControlWorkerTests, CompletesLifecycleAfterStopAndHostShutdown)
    {
        ChildControlWorkerHarness harness;
        ASSERT_TRUE(harness.Initialize());
        host::WorldServerHostStopSignal stopSignal;
        host::WorldServerHostChildControlWorker worker{harness.Control(), stopSignal};
        ASSERT_TRUE(worker.Start());

        std::string readyEvent;
        ASSERT_TRUE(harness.ReadEvent(&readyEvent));
        EXPECT_EQ(
            readyEvent,
            R"({"schema":"psnr.network_runtime.benchmark.control","version":1,"type":"ready","runId":"run-77","sequence":0})");

        ASSERT_TRUE(harness.WriteStop(7));
        ASSERT_TRUE(WaitForResult(worker, host::WorldServerHostChildControlWorkerResult::StopReceived));
        worker.NotifyShutdownCompleted(true);
        worker.Join();

        EXPECT_EQ(worker.Result(), host::WorldServerHostChildControlWorkerResult::Completed);
        EXPECT_EQ(worker.StopSequence(), static_cast<std::uint64_t>(7));
        EXPECT_TRUE(stopSignal.IsRequested());

        std::string stoppedEvent;
        ASSERT_TRUE(harness.ReadEvent(&stoppedEvent));
        EXPECT_EQ(
            stoppedEvent,
            R"({"schema":"psnr.network_runtime.benchmark.control","version":1,"type":"stopped","runId":"run-77","sequence":7})");
    }

    TEST(WorldServerHostChildControlWorkerTests, TreatsUnexpectedCommandPipeEndAsFailureAndRequestsStop)
    {
        ChildControlWorkerHarness harness;
        ASSERT_TRUE(harness.Initialize());
        host::WorldServerHostStopSignal stopSignal;
        host::WorldServerHostChildControlWorker worker{harness.Control(), stopSignal};
        ASSERT_TRUE(worker.Start());

        std::string readyEvent;
        ASSERT_TRUE(harness.ReadEvent(&readyEvent));
        harness.CloseCommandWriter();
        worker.Join();

        EXPECT_EQ(worker.Result(), host::WorldServerHostChildControlWorkerResult::ReadFailed);
        EXPECT_FALSE(worker.Failure().message.empty());
        EXPECT_TRUE(stopSignal.IsRequested());
    }

    TEST(WorldServerHostChildControlWorkerTests, WritesErrorWhenHostShutdownFails)
    {
        ChildControlWorkerHarness harness;
        ASSERT_TRUE(harness.Initialize());
        host::WorldServerHostStopSignal stopSignal;
        host::WorldServerHostChildControlWorker worker{harness.Control(), stopSignal};
        ASSERT_TRUE(worker.Start());

        std::string readyEvent;
        ASSERT_TRUE(harness.ReadEvent(&readyEvent));
        ASSERT_TRUE(harness.WriteStop(9));
        ASSERT_TRUE(WaitForResult(worker, host::WorldServerHostChildControlWorkerResult::StopReceived));
        worker.NotifyShutdownCompleted(false);
        worker.Join();

        EXPECT_EQ(worker.Result(), host::WorldServerHostChildControlWorkerResult::ShutdownFailed);
        std::string errorEvent;
        ASSERT_TRUE(harness.ReadEvent(&errorEvent));
        EXPECT_EQ(
            errorEvent,
            R"({"schema":"psnr.network_runtime.benchmark.control","version":1,"type":"error","runId":"run-77","sequence":9,"errorMessage":"host shutdown failed"})");
    }

    TEST(WorldServerHostChildControlWorkerTests, CancelsBlockingReadBeforeJoin)
    {
        ChildControlWorkerHarness harness;
        ASSERT_TRUE(harness.Initialize());
        host::WorldServerHostStopSignal stopSignal;
        host::WorldServerHostChildControlWorker worker{harness.Control(), stopSignal};
        ASSERT_TRUE(worker.Start());

        std::string readyEvent;
        ASSERT_TRUE(harness.ReadEvent(&readyEvent));
        worker.RequestStop();
        worker.Join();

        EXPECT_EQ(worker.Result(), host::WorldServerHostChildControlWorkerResult::Cancelled);
    }
} // namespace psnr::world::tests
