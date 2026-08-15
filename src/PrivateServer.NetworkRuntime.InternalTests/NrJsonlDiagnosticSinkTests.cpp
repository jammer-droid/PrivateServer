#include "pch.h"

#include "NrJsonlDiagnosticSink.h"

#include "NrErrorCode.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace psnr::runtime::internal
{
    namespace
    {
        using JsonObject = nlohmann::json;

        class ScopedJsonlArtifact final
        {
        public:
            explicit ScopedJsonlArtifact(const std::string_view suffix)
            {
                static std::atomic<std::uint64_t> nextId{0};
                const std::chrono::steady_clock::duration::rep timestamp =
                    std::chrono::steady_clock::now().time_since_epoch().count();
                const std::string fileName = "psnr-nrdiag-" + std::to_string(timestamp) + "-" +
                                             std::to_string(nextId.fetch_add(1, std::memory_order_relaxed)) + "-" +
                                             std::string(suffix) + ".jsonl";
                path_ = std::filesystem::temp_directory_path() / fileName;

                const std::u8string encoded = path_.u8string();
                pathUtf8_.assign(reinterpret_cast<const char*>(encoded.data()), encoded.size());

                std::error_code ignored;
                std::filesystem::remove(path_, ignored);
            }

            ~ScopedJsonlArtifact() noexcept
            {
                std::error_code ignored;
                std::filesystem::remove(path_, ignored);
            }

            [[nodiscard]] const std::string& PathUtf8() const noexcept
            {
                return pathUtf8_;
            }

            [[nodiscard]] std::vector<std::string> ReadLines() const
            {
                std::ifstream input(path_, std::ios::binary);
                std::vector<std::string> lines;
                std::string line;
                while (std::getline(input, line))
                {
                    lines.push_back(std::move(line));
                }
                return lines;
            }

        private:
            std::filesystem::path path_;
            std::string pathUtf8_;
        };

        template <typename TValue>
        [[nodiscard]] const TValue* FindValue(const JsonObject& object, const std::string_view key) noexcept
        {
            const JsonObject::const_iterator iterator = object.find(key);
            return iterator == object.end() ? nullptr : iterator->template get_ptr<const TValue*>();
        }

        [[nodiscard]] std::optional<JsonObject> ParseLine(const std::string& line)
        {
            JsonObject object = JsonObject::parse(line, nullptr, false);
            if (object.is_discarded() || !object.is_object())
            {
                return std::nullopt;
            }

            return object;
        }

        [[nodiscard]] std::unique_ptr<INrDiagnosticSink> CreateSink(const ScopedJsonlArtifact& artifact)
        {
            psnr::core::NrResult<std::unique_ptr<INrDiagnosticSink>> result =
                CreateJsonlDiagnosticSink(artifact.PathUtf8());
            EXPECT_TRUE(result.Succeeded());
            return result.Succeeded() ? result.TakeValue() : nullptr;
        }

        TEST(NrJsonlDiagnosticSinkTests, WritesParseableMetadataEventAndSummaryRows)
        {
            ScopedJsonlArtifact artifact("event");
            std::unique_ptr<INrDiagnosticSink> sink = CreateSink(artifact);
            ASSERT_NE(sink, nullptr);
            ASSERT_TRUE(sink->Begin(NrDiagnosticRunMetadata{NrDiagnosticsMode::Benchmark}).Succeeded());

            NrDiagnosticRecord record;
            record.producerTimestamp = 421337;
            record.drainSequence = 17;
            record.severity = NrDiagnosticSeverity::Error;
            record.eventKind = NrDiagnosticEventKind::Failure;
            record.component = NrDiagnosticComponent::IoPipeline;
            record.operation = NrDiagnosticOperation::Post;
            record.errorCode = psnr::core::NrErrorCode::IoFailed;
            record.nativeErrorCode = 10054;
            record.sessionKey = 42;
            record.ioOperation = NrDiagnosticIoOperation::Receive;
            record.closeReason = NrSessionEndReason::TransportError;
            record.contextFlags = static_cast<NrDiagnosticContextFlags>(
                static_cast<std::uint8_t>(NrDiagnosticContextFlags::HasSessionKey) |
                static_cast<std::uint8_t>(NrDiagnosticContextFlags::HasIoOperation) |
                static_cast<std::uint8_t>(NrDiagnosticContextFlags::HasCloseReason));
            ASSERT_TRUE(sink->Consume(record).Succeeded());

            const NrDiagnosticSummary summary{20, 19, 1, 0, 19, 0};
            ASSERT_TRUE(sink->Finish(summary).Succeeded());

            const std::vector<std::string> lines = artifact.ReadLines();
            ASSERT_EQ(lines.size(), 3u);

            const std::optional<JsonObject> run = ParseLine(lines[0]);
            ASSERT_TRUE(run.has_value());
            ASSERT_NE(FindValue<std::string>(*run, "schema"), nullptr);
            EXPECT_EQ(*FindValue<std::string>(*run, "schema"), "psnr.network_runtime.diagnostics");
            ASSERT_NE(FindValue<std::uint64_t>(*run, "version"), nullptr);
            EXPECT_EQ(*FindValue<std::uint64_t>(*run, "version"), 1u);
            ASSERT_NE(FindValue<std::string>(*run, "type"), nullptr);
            EXPECT_EQ(*FindValue<std::string>(*run, "type"), "run");
            ASSERT_NE(FindValue<std::string>(*run, "clock"), nullptr);
            EXPECT_EQ(*FindValue<std::string>(*run, "clock"), "steady_ns");
            ASSERT_NE(FindValue<std::string>(*run, "mode"), nullptr);
            EXPECT_EQ(*FindValue<std::string>(*run, "mode"), "benchmark");

            const std::optional<JsonObject> event = ParseLine(lines[1]);
            ASSERT_TRUE(event.has_value());
            ASSERT_NE(FindValue<std::string>(*event, "schema"), nullptr);
            EXPECT_EQ(*FindValue<std::string>(*event, "schema"), "psnr.network_runtime.diagnostics");
            ASSERT_NE(FindValue<std::uint64_t>(*event, "version"), nullptr);
            EXPECT_EQ(*FindValue<std::uint64_t>(*event, "version"), 1u);
            ASSERT_NE(FindValue<std::string>(*event, "type"), nullptr);
            EXPECT_EQ(*FindValue<std::string>(*event, "type"), "event");
            ASSERT_NE(FindValue<std::uint64_t>(*event, "tsNs"), nullptr);
            EXPECT_EQ(*FindValue<std::uint64_t>(*event, "tsNs"), 421337u);
            ASSERT_NE(FindValue<std::uint64_t>(*event, "sequence"), nullptr);
            EXPECT_EQ(*FindValue<std::uint64_t>(*event, "sequence"), 17u);
            ASSERT_NE(FindValue<std::string>(*event, "severity"), nullptr);
            EXPECT_EQ(*FindValue<std::string>(*event, "severity"), "error");
            ASSERT_NE(FindValue<std::string>(*event, "kind"), nullptr);
            EXPECT_EQ(*FindValue<std::string>(*event, "kind"), "failure");
            ASSERT_NE(FindValue<std::string>(*event, "component"), nullptr);
            EXPECT_EQ(*FindValue<std::string>(*event, "component"), "io_pipeline");
            ASSERT_NE(FindValue<std::string>(*event, "operation"), nullptr);
            EXPECT_EQ(*FindValue<std::string>(*event, "operation"), "post");
            ASSERT_NE(FindValue<std::uint64_t>(*event, "errorCode"), nullptr);
            EXPECT_EQ(*FindValue<std::uint64_t>(*event, "errorCode"),
                      static_cast<std::uint32_t>(psnr::core::NrErrorCode::IoFailed));
            ASSERT_NE(FindValue<std::uint64_t>(*event, "nativeErrorCode"), nullptr);
            EXPECT_EQ(*FindValue<std::uint64_t>(*event, "nativeErrorCode"), 10054u);
            ASSERT_NE(FindValue<std::uint64_t>(*event, "sessionKey"), nullptr);
            EXPECT_EQ(*FindValue<std::uint64_t>(*event, "sessionKey"), 42u);
            ASSERT_NE(FindValue<std::string>(*event, "ioOperation"), nullptr);
            EXPECT_EQ(*FindValue<std::string>(*event, "ioOperation"), "recv");
            ASSERT_NE(FindValue<std::string>(*event, "closeReason"), nullptr);
            EXPECT_EQ(*FindValue<std::string>(*event, "closeReason"), "transport_error");

            const std::optional<JsonObject> parsedSummary = ParseLine(lines[2]);
            ASSERT_TRUE(parsedSummary.has_value());
            ASSERT_NE(FindValue<std::string>(*parsedSummary, "schema"), nullptr);
            EXPECT_EQ(*FindValue<std::string>(*parsedSummary, "schema"), "psnr.network_runtime.diagnostics");
            ASSERT_NE(FindValue<std::uint64_t>(*parsedSummary, "version"), nullptr);
            EXPECT_EQ(*FindValue<std::uint64_t>(*parsedSummary, "version"), 1u);
            ASSERT_NE(FindValue<std::string>(*parsedSummary, "type"), nullptr);
            EXPECT_EQ(*FindValue<std::string>(*parsedSummary, "type"), "summary");
            ASSERT_NE(FindValue<std::uint64_t>(*parsedSummary, "attempted"), nullptr);
            EXPECT_EQ(*FindValue<std::uint64_t>(*parsedSummary, "attempted"), 20u);
            ASSERT_NE(FindValue<std::uint64_t>(*parsedSummary, "enqueued"), nullptr);
            EXPECT_EQ(*FindValue<std::uint64_t>(*parsedSummary, "enqueued"), 19u);
            ASSERT_NE(FindValue<std::uint64_t>(*parsedSummary, "consumed"), nullptr);
            EXPECT_EQ(*FindValue<std::uint64_t>(*parsedSummary, "consumed"), 19u);
            ASSERT_NE(FindValue<std::uint64_t>(*parsedSummary, "droppedQueueFull"), nullptr);
            EXPECT_EQ(*FindValue<std::uint64_t>(*parsedSummary, "droppedQueueFull"), 1u);
            ASSERT_NE(FindValue<std::uint64_t>(*parsedSummary, "droppedSinkUnavailable"), nullptr);
            EXPECT_EQ(*FindValue<std::uint64_t>(*parsedSummary, "droppedSinkUnavailable"), 0u);
            ASSERT_NE(FindValue<std::uint64_t>(*parsedSummary, "discardedAfterSinkFailure"), nullptr);
            EXPECT_EQ(*FindValue<std::uint64_t>(*parsedSummary, "discardedAfterSinkFailure"), 0u);
            ASSERT_NE(FindValue<bool>(*parsedSummary, "eventFlushSucceeded"), nullptr);
            EXPECT_TRUE(*FindValue<bool>(*parsedSummary, "eventFlushSucceeded"));
        }

        TEST(NrJsonlDiagnosticSinkTests, OmitsOptionalPropertiesAndPreservesUnknownEnumValues)
        {
            ScopedJsonlArtifact artifact("unknown");
            std::unique_ptr<INrDiagnosticSink> sink = CreateSink(artifact);
            ASSERT_NE(sink, nullptr);
            ASSERT_TRUE(sink->Begin(NrDiagnosticRunMetadata{NrDiagnosticsMode::Benchmark}).Succeeded());

            NrDiagnosticRecord record;
            record.severity = static_cast<NrDiagnosticSeverity>(0xff);
            record.eventKind = static_cast<NrDiagnosticEventKind>(0xfe);
            record.component = static_cast<NrDiagnosticComponent>(0xfffd);
            record.operation = static_cast<NrDiagnosticOperation>(0xfffc);
            ASSERT_TRUE(sink->Consume(record).Succeeded());

            NrDiagnosticRecord optionalRecord;
            optionalRecord.contextFlags = static_cast<NrDiagnosticContextFlags>(
                static_cast<std::uint8_t>(NrDiagnosticContextFlags::HasIoOperation) |
                static_cast<std::uint8_t>(NrDiagnosticContextFlags::HasCloseReason));
            optionalRecord.ioOperation = static_cast<NrDiagnosticIoOperation>(0xfd);
            optionalRecord.closeReason = static_cast<NrSessionEndReason>(0xfc);
            ASSERT_TRUE(sink->Consume(optionalRecord).Succeeded());
            ASSERT_TRUE(sink->Finish(NrDiagnosticSummary{2, 2, 0, 0, 2, 0}).Succeeded());

            const std::vector<std::string> lines = artifact.ReadLines();
            ASSERT_EQ(lines.size(), 4u);
            const std::optional<JsonObject> event = ParseLine(lines[1]);
            ASSERT_TRUE(event.has_value());

            ASSERT_NE(FindValue<std::string>(*event, "severity"), nullptr);
            EXPECT_EQ(*FindValue<std::string>(*event, "severity"), "unknown");
            ASSERT_NE(FindValue<std::uint64_t>(*event, "severityValue"), nullptr);
            EXPECT_EQ(*FindValue<std::uint64_t>(*event, "severityValue"), 0xffu);
            ASSERT_NE(FindValue<std::string>(*event, "kind"), nullptr);
            EXPECT_EQ(*FindValue<std::string>(*event, "kind"), "unknown");
            ASSERT_NE(FindValue<std::uint64_t>(*event, "kindValue"), nullptr);
            EXPECT_EQ(*FindValue<std::uint64_t>(*event, "kindValue"), 0xfeu);
            ASSERT_NE(FindValue<std::string>(*event, "component"), nullptr);
            EXPECT_EQ(*FindValue<std::string>(*event, "component"), "unknown");
            ASSERT_NE(FindValue<std::uint64_t>(*event, "componentValue"), nullptr);
            EXPECT_EQ(*FindValue<std::uint64_t>(*event, "componentValue"), 0xfffdu);
            ASSERT_NE(FindValue<std::string>(*event, "operation"), nullptr);
            EXPECT_EQ(*FindValue<std::string>(*event, "operation"), "unknown");
            ASSERT_NE(FindValue<std::uint64_t>(*event, "operationValue"), nullptr);
            EXPECT_EQ(*FindValue<std::uint64_t>(*event, "operationValue"), 0xfffcu);
            EXPECT_FALSE(event->contains("sessionKey"));
            EXPECT_FALSE(event->contains("ioOperation"));
            EXPECT_FALSE(event->contains("closeReason"));

            const std::optional<JsonObject> optionalEvent = ParseLine(lines[2]);
            ASSERT_TRUE(optionalEvent.has_value());
            ASSERT_NE(FindValue<std::string>(*optionalEvent, "ioOperation"), nullptr);
            EXPECT_EQ(*FindValue<std::string>(*optionalEvent, "ioOperation"), "unknown");
            ASSERT_NE(FindValue<std::uint64_t>(*optionalEvent, "ioOperationValue"), nullptr);
            EXPECT_EQ(*FindValue<std::uint64_t>(*optionalEvent, "ioOperationValue"), 0xfdu);
            ASSERT_NE(FindValue<std::string>(*optionalEvent, "closeReason"), nullptr);
            EXPECT_EQ(*FindValue<std::string>(*optionalEvent, "closeReason"), "unknown");
            ASSERT_NE(FindValue<std::uint64_t>(*optionalEvent, "closeReasonValue"), nullptr);
            EXPECT_EQ(*FindValue<std::uint64_t>(*optionalEvent, "closeReasonValue"), 0xfcu);
        }

        TEST(NrJsonlDiagnosticSinkTests, ZeroEventRunContainsMetadataAndTerminalSummary)
        {
            ScopedJsonlArtifact artifact("zero-event");
            std::unique_ptr<INrDiagnosticSink> sink = CreateSink(artifact);
            ASSERT_NE(sink, nullptr);
            ASSERT_TRUE(sink->Begin(NrDiagnosticRunMetadata{NrDiagnosticsMode::Benchmark}).Succeeded());
            ASSERT_TRUE(sink->Finish(NrDiagnosticSummary{}).Succeeded());

            const std::vector<std::string> lines = artifact.ReadLines();
            ASSERT_EQ(lines.size(), 2u);
            const std::optional<JsonObject> run = ParseLine(lines[0]);
            const std::optional<JsonObject> summary = ParseLine(lines[1]);
            ASSERT_TRUE(run.has_value());
            ASSERT_TRUE(summary.has_value());
            ASSERT_NE(FindValue<std::string>(*run, "type"), nullptr);
            EXPECT_EQ(*FindValue<std::string>(*run, "type"), "run");
            ASSERT_NE(FindValue<std::string>(*summary, "type"), nullptr);
            EXPECT_EQ(*FindValue<std::string>(*summary, "type"), "summary");
            ASSERT_NE(FindValue<std::uint64_t>(*summary, "attempted"), nullptr);
            EXPECT_EQ(*FindValue<std::uint64_t>(*summary, "attempted"), 0u);
            ASSERT_NE(FindValue<bool>(*summary, "eventFlushSucceeded"), nullptr);
            EXPECT_TRUE(*FindValue<bool>(*summary, "eventFlushSucceeded"));
        }
    } // namespace
} // namespace psnr::runtime::internal
