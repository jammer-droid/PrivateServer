#include "pch.h"

#include "ApplicationLogOutputFactory.h"

#include <spdlog/details/log_msg.h>
#include <spdlog/details/os.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/sink.h>
#include <spdlog/sinks/stdout_sinks.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#if defined(_WIN32)
#include <io.h>
#endif

namespace psnr::logging::internal
{
    namespace
    {
        constexpr std::string_view ApplicationLoggerName = "application";
        constexpr std::string_view PayloadOnlyPattern = "%v";

        void EnsureStdoutAvailable()
        {
#if defined(_WIN32)
            const int descriptor = _fileno(stdout);
            if (descriptor < 0 || _get_osfhandle(descriptor) == -1)
            {
                throw std::runtime_error("stdout is not available for application logging");
            }
#else
            if (stdout == nullptr)
            {
                throw std::runtime_error("stdout is not available for application logging");
            }
#endif
        }

        void WritePayload(spdlog::sinks::sink* const sink, const std::string_view payload)
        {
            const spdlog::details::log_msg message{
                spdlog::string_view_t{ApplicationLoggerName.data(), ApplicationLoggerName.size()}, spdlog::level::info,
                spdlog::string_view_t{payload.data(), payload.size()}};
            sink->log(message);
        }

        class SpdlogRotatingFileOutput final : public IApplicationLogOutput
        {
        public:
            SpdlogRotatingFileOutput(std::filesystem::path activePath, const std::size_t rotationBytes,
                                     const std::size_t rotationFileCount)
                : activePath_(std::move(activePath))
                , rotationBytes_(rotationBytes)
                , rotationFileCount_(rotationFileCount)
            {
                if (std::filesystem::exists(activePath_))
                {
                    throw std::invalid_argument("application log active file already exists");
                }

                OpenActiveFile(true);
            }

            void Write(const std::string_view payload) override
            {
                const std::size_t lineEndingSize = std::char_traits<char>::length(spdlog::details::os::default_eol);
                if (payload.size() > std::numeric_limits<std::size_t>::max() - lineEndingSize)
                {
                    throw std::length_error("application log payload size overflow");
                }

                const std::size_t lineSize = payload.size() + lineEndingSize;
                if (currentSize_ > 0 && (lineSize > rotationBytes_ || currentSize_ > rotationBytes_ - lineSize))
                {
                    RotateOnce();
                }

                WritePayload(sink_.get(), payload);
                currentSize_ = currentSize_ > std::numeric_limits<std::size_t>::max() - lineSize
                                   ? std::numeric_limits<std::size_t>::max()
                                   : currentSize_ + lineSize;
            }

            void Flush() override
            {
                sink_->flush();
            }

        private:
            [[nodiscard]] std::filesystem::path BackupPath(const std::size_t index) const
            {
                const std::string fileName =
                    activePath_.stem().string() + "." + std::to_string(index) + activePath_.extension().string();
                return activePath_.parent_path() / fileName;
            }

            void OpenActiveFile(const bool truncate)
            {
                sink_ = std::make_unique<spdlog::sinks::basic_file_sink_st>(activePath_.string(), truncate);
                sink_->set_pattern(std::string(PayloadOnlyPattern)); // 로그 끝에 줄바꿈 문자 사용 패턴
            }

            void RotateOnce()
            {
                sink_->flush(); // 현재 active sink flush
                sink_.reset();  // 및 close

                // 가장 오래된 backup 부터 삭제 시도
                for (std::size_t index = rotationFileCount_; index > 0; --index)
                {
                    const std::filesystem::path source = index == 1 ? activePath_ : BackupPath(index - 1);
                    const std::filesystem::path target = BackupPath(index);
                    if (!std::filesystem::exists(source))
                    {
                        continue;
                    }

                    if (std::filesystem::exists(target))
                    {
                        std::filesystem::remove(target);
                    }

                    /*
                        application.2.jsonl 삭제
                        application.1.jsonl → application.2.jsonl
                        application.jsonl   → application.1.jsonl
                        새 application.jsonl 생성
                    */
                    std::filesystem::rename(source, target); // backup 순서를 하나씩 밀어서 첫 번째를 남김
                }

                OpenActiveFile(true);
                currentSize_ = 0;
            }

            std::filesystem::path activePath_;
            std::size_t rotationBytes_ = 0;
            std::size_t rotationFileCount_ = 0;
            std::size_t currentSize_ = 0;
            std::unique_ptr<spdlog::sinks::basic_file_sink_st> sink_;
        };

        class SpdlogConsoleOutput final : public IApplicationLogOutput
        {
        public:
            SpdlogConsoleOutput()
            {
                sink_.set_pattern(std::string(PayloadOnlyPattern));
            }

            void Write(const std::string_view payload) override
            {
                WritePayload(&sink_, payload);
            }

            void Flush() override
            {
                if (std::fflush(stdout) != 0)
                {
                    throw std::runtime_error("failed to flush application log console output");
                }
            }

        private:
            spdlog::sinks::stdout_sink_st sink_;
        };
    } // namespace

    std::unique_ptr<IApplicationLogOutput> ApplicationLogOutputFactory::CreateRotatingFile(
        const ApplicationLogConfig& config)
    {
        if (!ApplicationLogConfig::IsValid(config) ||
            config.rotationBytes > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
        {
            throw std::invalid_argument("invalid rotating application log output config");
        }

        const std::filesystem::path activePath = config.outputDirectory / "application.jsonl";
        return std::make_unique<SpdlogRotatingFileOutput>(activePath, static_cast<std::size_t>(config.rotationBytes),
                                                          config.rotationFileCount);
    }

    std::unique_ptr<IApplicationLogOutput> ApplicationLogOutputFactory::CreateConsole()
    {
        EnsureStdoutAvailable();
        return std::make_unique<SpdlogConsoleOutput>();
    }
} // namespace psnr::logging::internal
