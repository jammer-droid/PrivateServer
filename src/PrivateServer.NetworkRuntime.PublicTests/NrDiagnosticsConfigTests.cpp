#include "pch.h"

#include <PrivateServer/NetworkRuntime/NrDiagnosticsConfig.h>
#include <PrivateServer/NetworkRuntime/NrServer.h>

#include "NrServerTestUtils.h"

#include <cstdint>
#include <string>
#include <type_traits>

namespace psnr::runtime
{
    using psnr::core::NrErrorCode;
    using psnr::core::NrStatus;
    using tests::CreateServerConfig;
    using tests::ExpectStatus;

    namespace
    {
        [[nodiscard]] NrUtf8View MakeUtf8View(const std::string& value) noexcept
        {
            return NrUtf8View{value.data(), static_cast<std::uint32_t>(value.size())};
        }

        [[nodiscard]] NrStatus CreateWithDiagnostics(const NrDiagnosticsConfig diagnostics) noexcept
        {
            NrServerConfig config = CreateServerConfig();
            config.diagnostics = diagnostics;

            NrServer server;
            return NrServer::Create(config, &server);
        }

        TEST(NrDiagnosticsConfigTests, PublicTypesKeepFixedValueContract)
        {
            const NrDiagnosticsConfig config;

            EXPECT_EQ(config.mode, NrDiagnosticsMode::Disabled);
            EXPECT_EQ(config.outputPath.data, nullptr);
            EXPECT_EQ(config.outputPath.size, 0u);
            EXPECT_TRUE((std::is_same_v<std::underlying_type_t<NrDiagnosticsMode>, std::uint8_t>));
            EXPECT_TRUE(std::is_trivially_copyable_v<NrUtf8View>);
            EXPECT_TRUE(std::is_trivially_copyable_v<NrDiagnosticsConfig>);
        }

        TEST(NrDiagnosticsConfigTests, CreateAcceptsSupportedModePathCombinations)
        {
            ExpectStatus(CreateWithDiagnostics(NrDiagnosticsConfig{}), NrStatus::Success());
            ExpectStatus(CreateWithDiagnostics(NrDiagnosticsConfig{NrDiagnosticsMode::Debug, {}}), NrStatus::Success());

            const std::string outputPath = "artifacts/diagnostics.jsonl";
            ExpectStatus(
                CreateWithDiagnostics(NrDiagnosticsConfig{NrDiagnosticsMode::Benchmark, MakeUtf8View(outputPath)}),
                NrStatus::Success());
        }

        TEST(NrDiagnosticsConfigTests, CreateRejectsPathForDisabledAndDebugModes)
        {
            const std::string outputPath = "diagnostics.jsonl";
            const NrUtf8View outputPathView = MakeUtf8View(outputPath);

            ExpectStatus(CreateWithDiagnostics(NrDiagnosticsConfig{NrDiagnosticsMode::Disabled, outputPathView}),
                         NrStatus::Failure(NrErrorCode::InvalidArgument));
            ExpectStatus(CreateWithDiagnostics(NrDiagnosticsConfig{NrDiagnosticsMode::Debug, outputPathView}),
                         NrStatus::Failure(NrErrorCode::InvalidArgument));
        }

        TEST(NrDiagnosticsConfigTests, CreateRejectsMissingBenchmarkPath)
        {
            ExpectStatus(CreateWithDiagnostics(NrDiagnosticsConfig{NrDiagnosticsMode::Benchmark, {}}),
                         NrStatus::Failure(NrErrorCode::InvalidArgument));
            ExpectStatus(
                CreateWithDiagnostics(NrDiagnosticsConfig{NrDiagnosticsMode::Benchmark, NrUtf8View{nullptr, 1}}),
                NrStatus::Failure(NrErrorCode::InvalidArgument));
        }

        TEST(NrDiagnosticsConfigTests, CreateRejectsInvalidBenchmarkUtf8)
        {
            const char overlong[] = {static_cast<char>(0xC0), static_cast<char>(0xAF)};
            const char surrogate[] = {static_cast<char>(0xED), static_cast<char>(0xA0), static_cast<char>(0x80)};
            const char outOfRange[] = {static_cast<char>(0xF4), static_cast<char>(0x90), static_cast<char>(0x80),
                                       static_cast<char>(0x80)};
            const char truncated[] = {static_cast<char>(0xE2), static_cast<char>(0x82)};

            const NrUtf8View invalidPaths[] = {
                NrUtf8View{overlong, static_cast<std::uint32_t>(sizeof(overlong))},
                NrUtf8View{surrogate, static_cast<std::uint32_t>(sizeof(surrogate))},
                NrUtf8View{outOfRange, static_cast<std::uint32_t>(sizeof(outOfRange))},
                NrUtf8View{truncated, static_cast<std::uint32_t>(sizeof(truncated))},
            };

            for (const NrUtf8View invalidPath : invalidPaths)
            {
                ExpectStatus(CreateWithDiagnostics(NrDiagnosticsConfig{NrDiagnosticsMode::Benchmark, invalidPath}),
                             NrStatus::Failure(NrErrorCode::InvalidArgument));
            }
        }

        TEST(NrDiagnosticsConfigTests, CreateRejectsEmbeddedNul)
        {
            const char outputPath[] = {'a', '\0', 'b'};

            ExpectStatus(CreateWithDiagnostics(NrDiagnosticsConfig{
                             NrDiagnosticsMode::Benchmark,
                             NrUtf8View{outputPath, static_cast<std::uint32_t>(sizeof(outputPath))}}),
                         NrStatus::Failure(NrErrorCode::InvalidArgument));
        }

        TEST(NrDiagnosticsConfigTests, CreateRejectsUnknownMode)
        {
            NrDiagnosticsConfig diagnostics;
            diagnostics.mode = static_cast<NrDiagnosticsMode>(0xFF);

            ExpectStatus(CreateWithDiagnostics(diagnostics), NrStatus::Failure(NrErrorCode::InvalidArgument));
        }

        TEST(NrDiagnosticsConfigTests, BenchmarkPathStorageMayBeReleasedAfterCreate)
        {
            NrServer server;
            {
                std::string outputPath = "artifacts/diagnostics.jsonl";
                NrServerConfig config = CreateServerConfig();
                config.diagnostics = NrDiagnosticsConfig{NrDiagnosticsMode::Benchmark, MakeUtf8View(outputPath)};

                ExpectStatus(NrServer::Create(config, &server), NrStatus::Success());
                outputPath.assign("caller storage was replaced");
            }

            EXPECT_TRUE(server.IsValid());
        }
    } // namespace
} // namespace psnr::runtime
