#include "pch.h"

#include "ApplicationLogOutputFactory.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace psnr::logging::internal
{
    namespace
    {
        class ScopedApplicationLogDirectory final
        {
        public:
            explicit ScopedApplicationLogDirectory(const std::string_view suffix)
            {
                static std::atomic<std::uint64_t> nextId{0};
                const std::chrono::steady_clock::duration::rep timestamp =
                    std::chrono::steady_clock::now().time_since_epoch().count();

                for (std::uint32_t attempt = 0; attempt < 100; ++attempt)
                {
                    const std::uint64_t uniqueId = nextId.fetch_add(1, std::memory_order_relaxed);
                    const std::filesystem::path candidate =
                        std::filesystem::temp_directory_path() /
                        ("psnr-application-log-" + std::to_string(timestamp) + "-" + std::to_string(uniqueId) + "-" +
                         std::string(suffix));
                    std::error_code createError;
                    if (std::filesystem::create_directory(candidate, createError))
                    {
                        path_ = candidate;
                        ownsPath_ = true;
                        return;
                    }

                    if (createError)
                    {
                        throw std::filesystem::filesystem_error("failed to create application log test directory",
                                                                candidate, createError);
                    }
                }

                throw std::runtime_error("failed to reserve a unique application log test directory");
            }

            ~ScopedApplicationLogDirectory() noexcept
            {
                if (ownsPath_)
                {
                    std::error_code ignored;
                    std::filesystem::remove_all(path_, ignored);
                }
            }

            [[nodiscard]] const std::filesystem::path& Path() const noexcept
            {
                return path_;
            }

            [[nodiscard]] std::string ReadFile(const std::filesystem::path& path) const
            {
                std::ifstream input(path, std::ios::binary);
                const std::istreambuf_iterator<char> begin{input};
                const std::istreambuf_iterator<char> end{};
                return std::string(begin, end);
            }

        private:
            std::filesystem::path path_;
            bool ownsPath_ = false;
        };

        [[nodiscard]] ApplicationLogConfig MakeConfig(const std::filesystem::path& outputDirectory)
        {
            ApplicationLogConfig config{};
            config.runId = "run-20260801T153012.482Z-550e8400-e29b-41d4-a716-446655440000";
            config.process = "world-server-host";
            config.outputDirectory = outputDirectory;
            return config;
        }
    } // namespace

    TEST(ApplicationLogOutputFactoryTests, RotatingFileAddsExactlyOneWindowsNewlinePerPayload)
    {
        const ScopedApplicationLogDirectory directory{"newline"};
        const ApplicationLogConfig config = MakeConfig(directory.Path());
        std::unique_ptr<IApplicationLogOutput> output = ApplicationLogOutputFactory::CreateRotatingFile(config);

        ASSERT_NE(output, nullptr);
        output->Write(R"({"event":"first"})");
        output->Write(R"({"event":"second"})");
        output->Flush();

        const std::string content = directory.ReadFile(directory.Path() / "application.jsonl");
        EXPECT_EQ(content, "{\"event\":\"first\"}\r\n{\"event\":\"second\"}\r\n");
    }

    TEST(ApplicationLogOutputFactoryTests, RotatesBeforeWritingPayloadThatExceedsConfiguredSegmentSize)
    {
        const ScopedApplicationLogDirectory directory{"rotation"};
        ApplicationLogConfig config = MakeConfig(directory.Path());
        config.rotationBytes = 16;
        config.rotationFileCount = 2;
        std::unique_ptr<IApplicationLogOutput> output = ApplicationLogOutputFactory::CreateRotatingFile(config);

        output->Write("1234567890");
        output->Write("abcdefghij");
        output->Write("ABCDEFGHIJ");
        output->Write("klmnopqrst");
        output->Flush();

        EXPECT_EQ(directory.ReadFile(directory.Path() / "application.2.jsonl"), "abcdefghij\r\n");
        EXPECT_EQ(directory.ReadFile(directory.Path() / "application.1.jsonl"), "ABCDEFGHIJ\r\n");
        EXPECT_EQ(directory.ReadFile(directory.Path() / "application.jsonl"), "klmnopqrst\r\n");
        EXPECT_FALSE(std::filesystem::exists(directory.Path() / "application.3.jsonl"));
    }

    TEST(ApplicationLogOutputFactoryTests, ReportsFileOpenFailureToCaller)
    {
        const ScopedApplicationLogDirectory directory{"open-failure"};
        const std::filesystem::path blockingFile = directory.Path() / "blocked";
        {
            std::ofstream output(blockingFile, std::ios::binary);
            output << "not a directory";
        }

        const ApplicationLogConfig config = MakeConfig(blockingFile);

        EXPECT_THROW(ApplicationLogOutputFactory::CreateRotatingFile(config), std::exception);
    }

    TEST(ApplicationLogOutputFactoryTests, SurfacesRotationFailureWithoutRetry)
    {
        const ScopedApplicationLogDirectory directory{"rotation-failure"};
        ApplicationLogConfig config = MakeConfig(directory.Path());
        config.rotationBytes = 16;
        config.rotationFileCount = 1;
        std::unique_ptr<IApplicationLogOutput> output = ApplicationLogOutputFactory::CreateRotatingFile(config);
        output->Write("1234567890");

        const std::filesystem::path blockedBackup = directory.Path() / "application.1.jsonl";
        ASSERT_TRUE(std::filesystem::create_directory(blockedBackup));
        {
            std::ofstream blocker(blockedBackup / "keep", std::ios::binary);
            blocker << "prevent directory removal";
        }

        EXPECT_THROW(output->Write("abcdefghij"), std::filesystem::filesystem_error);
        EXPECT_TRUE(std::filesystem::exists(blockedBackup / "keep"));
    }

    TEST(ApplicationLogOutputFactoryTests, RejectsExistingActiveFileInsteadOfAppending)
    {
        const ScopedApplicationLogDirectory directory{"existing-active"};
        const std::filesystem::path activePath = directory.Path() / "application.jsonl";
        {
            std::ofstream output(activePath, std::ios::binary);
            output << "existing";
        }

        const ApplicationLogConfig config = MakeConfig(directory.Path());

        EXPECT_THROW(ApplicationLogOutputFactory::CreateRotatingFile(config), std::invalid_argument);
        EXPECT_EQ(directory.ReadFile(activePath), "existing");
    }

    TEST(ApplicationLogOutputFactoryTests, CreatesConsoleOutputWithoutExposingSpdlog)
    {
        std::unique_ptr<IApplicationLogOutput> output = ApplicationLogOutputFactory::CreateConsole();

        ASSERT_NE(output, nullptr);
        output->Flush();
    }
} // namespace psnr::logging::internal
