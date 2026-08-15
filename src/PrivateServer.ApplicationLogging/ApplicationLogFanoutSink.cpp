#include "pch.h"

#include "ApplicationLogFanoutSink.h"

#include "ApplicationLogFormatter.h"
#include "ApplicationLogPayloadCodec.h"

#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace psnr::logging::internal
{
    ApplicationLogFanoutSink::ApplicationLogFanoutSink(ApplicationLogConfig config,
                                                       std::unique_ptr<IApplicationLogOutput> fileOutput,
                                                       std::unique_ptr<IApplicationLogOutput> consoleOutput)
        : config_(std::move(config))
        , fileOutput_(std::move(fileOutput))
        , consoleOutput_(std::move(consoleOutput))
        , fileSinkFailed_(fileOutput_ == nullptr)
        , consoleSinkFailed_(consoleOutput_ == nullptr)
    {
        if (!ApplicationLogConfig::IsValid(config_))
        {
            throw std::invalid_argument("invalid application log config");
        }

        if (fileOutput_ == nullptr && consoleOutput_ == nullptr)
        {
            throw std::invalid_argument("application log fanout requires at least one output");
        }
    }

    void ApplicationLogFanoutSink::Consume(const std::string_view transportPayload)
    {
        const bool fileSinkFailed = fileSinkFailed_.load(std::memory_order_relaxed);
        const bool consoleSinkFailed = consoleSinkFailed_.load(std::memory_order_relaxed);
        if (fileSinkFailed && consoleSinkFailed)
        {
            IncrementSaturating(discardedAfterSinkFailure_);
            return;
        }

        try
        {
            ApplicationLogEnvelope envelope{};
            if (!ApplicationLogPayloadCodec::TryDecode(transportPayload, &envelope))
            {
                throw std::invalid_argument("invalid application log transport payload");
            }

            const std::uint64_t drainSequence = NextDrainSequence();
            const ApplicationLogFormatInput input{config_, envelope, drainSequence};
            const std::string filePayload = ApplicationLogFormatter::FormatJsonPayload(input);
            const std::string consolePayload = ApplicationLogFormatter::FormatConsolePayload(input);

            WriteIfHealthy(fileOutput_.get(), fileSinkFailed_, filePayload);
            WriteIfHealthy(consoleOutput_.get(), consoleSinkFailed_, consolePayload);
        }
        catch (...)
        {
            IncrementSaturating(consumed_);
            throw;
        }

        IncrementSaturating(consumed_);
    }

    void ApplicationLogFanoutSink::Flush() noexcept
    {
        FlushIfHealthy(fileOutput_.get(), fileSinkFailed_);
        FlushIfHealthy(consoleOutput_.get(), consoleSinkFailed_);
    }

    ApplicationLogFanoutSnapshot ApplicationLogFanoutSink::Snapshot() const noexcept
    {
        ApplicationLogFanoutSnapshot snapshot{};
        snapshot.fileSinkFailed = fileSinkFailed_.load(std::memory_order_relaxed);
        snapshot.consoleSinkFailed = consoleSinkFailed_.load(std::memory_order_relaxed);
        snapshot.consumed = consumed_.load(std::memory_order_relaxed);
        snapshot.discardedAfterSinkFailure = discardedAfterSinkFailure_.load(std::memory_order_relaxed);
        return snapshot;
    }

    void ApplicationLogFanoutSink::IncrementSaturating(std::atomic<std::uint64_t>& counter) noexcept
    {
        std::uint64_t current = counter.load(std::memory_order_relaxed);
        while (current != std::numeric_limits<std::uint64_t>::max() &&
               !counter.compare_exchange_weak(current, current + 1, std::memory_order_relaxed))
        {
        }
    }

    void ApplicationLogFanoutSink::WriteIfHealthy(IApplicationLogOutput* const output, std::atomic<bool>& failed,
                                                  const std::string& payload) noexcept
    {
        if (output == nullptr || failed.load(std::memory_order_relaxed))
        {
            return;
        }

        try
        {
            output->Write(payload);
        }
        catch (...)
        {
            failed.store(true, std::memory_order_relaxed);
        }
    }

    void ApplicationLogFanoutSink::FlushIfHealthy(IApplicationLogOutput* const output,
                                                  std::atomic<bool>& failed) noexcept
    {
        if (output == nullptr || failed.load(std::memory_order_relaxed))
        {
            return;
        }

        try
        {
            output->Flush();
        }
        catch (...)
        {
            failed.store(true, std::memory_order_relaxed);
        }
    }

    std::uint64_t ApplicationLogFanoutSink::NextDrainSequence() noexcept
    {
        if (drainSequence_ != std::numeric_limits<std::uint64_t>::max())
        {
            ++drainSequence_;
        }

        return drainSequence_;
    }
} // namespace psnr::logging::internal
