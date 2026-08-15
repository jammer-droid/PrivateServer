#include "pch.h"

#include "WorldServerHostChildControl.h"

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
        class TestOwnedHandle final
        {
        public:
            TestOwnedHandle() noexcept = default;
            explicit TestOwnedHandle(HANDLE handle) noexcept
                : handle_(handle)
            {
            }

            ~TestOwnedHandle() noexcept
            {
                Reset();
            }

            TestOwnedHandle(const TestOwnedHandle&) = delete;
            TestOwnedHandle& operator=(const TestOwnedHandle&) = delete;

            TestOwnedHandle(TestOwnedHandle&& other) noexcept
                : handle_(other.handle_)
            {
                other.handle_ = nullptr;
            }

            TestOwnedHandle& operator=(TestOwnedHandle&& other) noexcept
            {
                if (this != &other)
                {
                    Reset();
                    handle_ = other.handle_;
                    other.handle_ = nullptr;
                }
                return *this;
            }

            [[nodiscard]] HANDLE Get() const noexcept
            {
                return handle_;
            }

            [[nodiscard]] HANDLE Release() noexcept
            {
                const HANDLE handle = handle_;
                handle_ = nullptr;
                return handle;
            }

            void Reset(HANDLE handle = nullptr) noexcept
            {
                if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE)
                {
                    static_cast<void>(CloseHandle(handle_));
                }
                handle_ = handle;
            }

        private:
            HANDLE handle_ = nullptr;
        };

        class ChildControlHarness final
        {
        public:
            [[nodiscard]] bool Initialize(const std::string_view runId)
            {
                HANDLE commandRead = nullptr;
                HANDLE commandWrite = nullptr;
                if (CreatePipe(&commandRead, &commandWrite, nullptr, 0) == FALSE)
                {
                    return false;
                }
                TestOwnedHandle commandReadOwner{commandRead};
                commandWrite_.Reset(commandWrite);

                HANDLE eventRead = nullptr;
                HANDLE eventWrite = nullptr;
                if (CreatePipe(&eventRead, &eventWrite, nullptr, 0) == FALSE)
                {
                    return false;
                }
                eventRead_.Reset(eventRead);
                TestOwnedHandle eventWriteOwner{eventWrite};

                WorldResult<std::unique_ptr<host::WorldServerHostChildControl>,
                            host::WorldServerHostChildControlFailure>
                    controlResult = host::WorldServerHostChildControl::Create(
                        std::string{runId}, reinterpret_cast<std::uintptr_t>(commandReadOwner.Get()),
                        reinterpret_cast<std::uintptr_t>(eventWriteOwner.Get()));
                if (controlResult.Failed())
                {
                    return false;
                }

                control_ = controlResult.TakeValue();
                static_cast<void>(commandReadOwner.Release());
                static_cast<void>(eventWriteOwner.Release());
                return true;
            }

            [[nodiscard]] host::WorldServerHostChildControl& Control() const noexcept
            {
                return *control_;
            }

            [[nodiscard]] bool WriteCommand(const std::string_view command) const noexcept
            {
                std::string framedCommand{command};
                framedCommand.push_back('\n');
                DWORD writtenBytes = 0;
                return WriteFile(commandWrite_.Get(), framedCommand.data(), static_cast<DWORD>(framedCommand.size()),
                                 &writtenBytes, nullptr) != FALSE &&
                       writtenBytes == framedCommand.size();
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
                    if (ReadFile(eventRead_.Get(), &byte, 1, &readBytes, nullptr) == FALSE || readBytes != 1)
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

            [[nodiscard]] bool CloseControlAndObserveEventEnd()
            {
                control_.reset();
                char byte = '\0';
                DWORD readBytes = 0;
                const BOOL readSucceeded = ReadFile(eventRead_.Get(), &byte, 1, &readBytes, nullptr);
                return (readSucceeded == FALSE && GetLastError() == ERROR_BROKEN_PIPE) ||
                       (readSucceeded != FALSE && readBytes == 0);
            }

        private:
            TestOwnedHandle commandWrite_;
            TestOwnedHandle eventRead_;
            std::unique_ptr<host::WorldServerHostChildControl> control_;
        };
    } // namespace

    TEST(WorldServerHostChildControlTests, ReadsWireCompatibleStopCommand)
    {
        ChildControlHarness harness;
        ASSERT_TRUE(harness.Initialize("run-77"));
        ASSERT_TRUE(harness.WriteCommand(
            R"({"schema":"psnr.network_runtime.benchmark.control","version":1,"type":"stop","runId":"run-77","sequence":7})"));

        WorldResult<host::WorldServerHostChildControlCommand, host::WorldServerHostChildControlFailure> stopResult =
            harness.Control().ReadCommand();

        ASSERT_TRUE(stopResult.Succeeded()) << stopResult.Error().message;
        EXPECT_EQ(stopResult.Value().sequence, static_cast<std::uint64_t>(7));
    }

    TEST(WorldServerHostChildControlTests, WritesWireCompatibleLifecycleEvents)
    {
        ChildControlHarness harness;
        ASSERT_TRUE(harness.Initialize("run-77"));

        ASSERT_TRUE(harness.Control().WriteReady().Succeeded());
        ASSERT_TRUE(harness.Control().WriteStopped(7).Succeeded());
        ASSERT_TRUE(harness.Control().WriteError(8, "child failed").Succeeded());

        std::string ready;
        std::string stopped;
        std::string error;
        ASSERT_TRUE(harness.ReadEvent(&ready));
        ASSERT_TRUE(harness.ReadEvent(&stopped));
        ASSERT_TRUE(harness.ReadEvent(&error));
        EXPECT_EQ(
            ready,
            R"({"schema":"psnr.network_runtime.benchmark.control","version":1,"type":"ready","runId":"run-77","sequence":0})");
        EXPECT_EQ(
            stopped,
            R"({"schema":"psnr.network_runtime.benchmark.control","version":1,"type":"stopped","runId":"run-77","sequence":7})");
        EXPECT_EQ(
            error,
            R"({"schema":"psnr.network_runtime.benchmark.control","version":1,"type":"error","runId":"run-77","sequence":8,"errorMessage":"child failed"})");
    }

    TEST(WorldServerHostChildControlTests, RejectsWrongRunIdUnsupportedCommandAndInvalidEvent)
    {
        ChildControlHarness wrongRunHarness;
        ASSERT_TRUE(wrongRunHarness.Initialize("run-77"));
        ASSERT_TRUE(wrongRunHarness.WriteCommand(
            R"({"schema":"psnr.network_runtime.benchmark.control","version":1,"type":"stop","runId":"other-run","sequence":1})"));
        EXPECT_TRUE(wrongRunHarness.Control().ReadCommand().Failed());

        ChildControlHarness unsupportedHarness;
        ASSERT_TRUE(unsupportedHarness.Initialize("run-77"));
        ASSERT_TRUE(unsupportedHarness.WriteCommand(
            R"({"schema":"psnr.network_runtime.benchmark.control","version":1,"type":"captureSnapshot","runId":"run-77","sequence":2})"));
        EXPECT_TRUE(unsupportedHarness.Control().ReadCommand().Failed());

        EXPECT_TRUE(unsupportedHarness.Control().WriteStopped(0).Failed());
        EXPECT_TRUE(unsupportedHarness.Control().WriteError(1, {}).Failed());
    }

    TEST(WorldServerHostChildControlTests, OwnsAndClosesInheritedPipeEndpoints)
    {
        ChildControlHarness harness;
        ASSERT_TRUE(harness.Initialize("run-77"));

        EXPECT_TRUE(harness.CloseControlAndObserveEventEnd());
    }
} // namespace psnr::world::tests
