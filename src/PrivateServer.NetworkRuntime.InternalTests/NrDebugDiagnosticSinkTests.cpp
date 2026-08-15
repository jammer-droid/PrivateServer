#include "pch.h"

#include "NrDebugDiagnosticSink.h"

#include "NrErrorCode.h"

#include <cstdint>
#include <string_view>

namespace psnr::runtime::internal
{
    namespace
    {
        TEST(NrDebugDiagnosticSinkTests, FormatsStableDebugLineWithFlaggedContext)
        {
            NrDiagnosticRecord record;
            record.producerTimestamp = 421337;
            record.drainSequence = 17;
            record.sessionKey = 42;
            record.errorCode = psnr::core::NrErrorCode::IoFailed;
            record.nativeErrorCode = 10054;
            record.component = NrDiagnosticComponent::IoPipeline;
            record.operation = NrDiagnosticOperation::Post;
            record.severity = NrDiagnosticSeverity::Error;
            record.eventKind = NrDiagnosticEventKind::Failure;
            record.contextFlags = static_cast<NrDiagnosticContextFlags>(
                static_cast<std::uint8_t>(NrDiagnosticContextFlags::HasSessionKey) |
                static_cast<std::uint8_t>(NrDiagnosticContextFlags::HasIoOperation));
            record.ioOperation = NrDiagnosticIoOperation::Receive;

            NrDebugDiagnosticLine line;
            ASSERT_TRUE(FormatDebugDiagnosticRecord(record, line).Succeeded());
            EXPECT_EQ(line.View(),
                      "nrdiag seq=17 ts_ns=421337 severity=error kind=failure component=io_pipeline "
                      "operation=post error=io_failed native=10054 session=42 io=recv\n");
        }

        TEST(NrDebugDiagnosticSinkTests, OmitsContextWithoutCorrespondingFlags)
        {
            NrDiagnosticRecord record;
            record.sessionKey = 99;
            record.ioOperation = NrDiagnosticIoOperation::Send;
            record.closeReason = NrSessionEndReason::ProtocolError;

            NrDebugDiagnosticLine line;
            ASSERT_TRUE(FormatDebugDiagnosticRecord(record, line).Succeeded());

            const std::string_view view = line.View();
            EXPECT_EQ(view.find(" session="), std::string_view::npos);
            EXPECT_EQ(view.find(" io="), std::string_view::npos);
            EXPECT_EQ(view.find(" close_reason="), std::string_view::npos);
        }

        TEST(NrDebugDiagnosticSinkTests, DistinguishesSameErrorByComponentAndOperation)
        {
            NrDiagnosticRecord postFailure;
            postFailure.errorCode = psnr::core::NrErrorCode::IoFailed;
            postFailure.component = NrDiagnosticComponent::IoPipeline;
            postFailure.operation = NrDiagnosticOperation::Post;

            NrDiagnosticRecord lifecycleFailure = postFailure;
            lifecycleFailure.component = NrDiagnosticComponent::ServerLifecycle;
            lifecycleFailure.operation = NrDiagnosticOperation::Start;

            NrDebugDiagnosticLine postLine;
            NrDebugDiagnosticLine lifecycleLine;
            ASSERT_TRUE(FormatDebugDiagnosticRecord(postFailure, postLine).Succeeded());
            ASSERT_TRUE(FormatDebugDiagnosticRecord(lifecycleFailure, lifecycleLine).Succeeded());

            EXPECT_NE(postLine.View(), lifecycleLine.View());
            EXPECT_NE(postLine.View().find("component=io_pipeline operation=post"), std::string_view::npos);
            EXPECT_NE(lifecycleLine.View().find("component=server_lifecycle operation=start"),
                      std::string_view::npos);
        }

        TEST(NrDebugDiagnosticSinkTests, UnknownEnumsRemainBoundedStableTokens)
        {
            NrDiagnosticRecord record;
            record.component = static_cast<NrDiagnosticComponent>(0xffff);
            record.operation = static_cast<NrDiagnosticOperation>(0xffff);
            record.severity = static_cast<NrDiagnosticSeverity>(0xff);
            record.eventKind = static_cast<NrDiagnosticEventKind>(0xff);
            record.errorCode = static_cast<psnr::core::NrErrorCode>(0x7fffffff);
            record.contextFlags = static_cast<NrDiagnosticContextFlags>(
                static_cast<std::uint8_t>(NrDiagnosticContextFlags::HasIoOperation) |
                static_cast<std::uint8_t>(NrDiagnosticContextFlags::HasCloseReason));
            record.ioOperation = static_cast<NrDiagnosticIoOperation>(0xff);
            record.closeReason = static_cast<NrSessionEndReason>(0xff);

            NrDebugDiagnosticLine line;
            ASSERT_TRUE(FormatDebugDiagnosticRecord(record, line).Succeeded());

            const std::string_view view = line.View();
            EXPECT_LT(view.size(), NrDebugDiagnosticLine::Capacity);
            EXPECT_NE(view.find("severity=unknown kind=unknown component=unknown operation=unknown error=unknown"),
                      std::string_view::npos);
            EXPECT_NE(view.find("io=unknown close_reason=unknown"), std::string_view::npos);
        }

        TEST(NrDebugDiagnosticSinkTests, DebugSinkAcceptsOnlyDebugRunMetadata)
        {
            psnr::core::NrResult<std::unique_ptr<INrDiagnosticSink>> invalidSinkResult =
                CreateDebugDiagnosticSink();
            ASSERT_TRUE(invalidSinkResult.Succeeded());
            std::unique_ptr<INrDiagnosticSink> invalidSink = invalidSinkResult.TakeValue();
            EXPECT_EQ(invalidSink->Begin(NrDiagnosticRunMetadata{NrDiagnosticsMode::Benchmark}).ErrorCode(),
                      psnr::core::NrErrorCode::InvalidArgument);

            psnr::core::NrResult<std::unique_ptr<INrDiagnosticSink>> sinkResult = CreateDebugDiagnosticSink();
            ASSERT_TRUE(sinkResult.Succeeded());
            std::unique_ptr<INrDiagnosticSink> sink = sinkResult.TakeValue();
            EXPECT_TRUE(sink->Begin(NrDiagnosticRunMetadata{NrDiagnosticsMode::Debug}).Succeeded());
            EXPECT_TRUE(sink->Finish(NrDiagnosticSummary{}).Succeeded());
        }
    } // namespace
} // namespace psnr::runtime::internal
