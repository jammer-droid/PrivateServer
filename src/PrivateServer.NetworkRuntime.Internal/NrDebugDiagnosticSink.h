#pragma once

#include "NrDiagnosticRecord.h"
#include "NrDiagnosticSink.h"
#include "NrResult.h"

#include <array>
#include <cstddef>
#include <memory>
#include <string_view>

namespace psnr::runtime::internal
{
    // Fixed-capacity UTF-8 line produced by the Debug stderr adapter.
    struct NrDebugDiagnosticLine final
    {
        static constexpr std::size_t Capacity = 512;

        [[nodiscard]] std::string_view View() const noexcept
        {
            return std::string_view(bytes.data(), length);
        }

        std::array<char, Capacity> bytes{};
        std::size_t length = 0;
    };

    [[nodiscard]] psnr::core::NrStatus FormatDebugDiagnosticRecord(const NrDiagnosticRecord& record,
                                                                   NrDebugDiagnosticLine& output) noexcept;

    [[nodiscard]] psnr::core::NrResult<std::unique_ptr<INrDiagnosticSink>> CreateDebugDiagnosticSink() noexcept;
} // namespace psnr::runtime::internal
