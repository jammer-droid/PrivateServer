#include "pch.h"

#include "NrDiagnosticsConfigInternal.h"

#include "NrErrorCode.h"

#include <cstddef>
#include <cstdint>
#include <new>
#include <stdexcept>
#include <utility>

namespace psnr::runtime::internal
{
    using psnr::core::NrErrorCode;
    using psnr::core::NrResult;

    namespace
    {
        [[nodiscard]] constexpr bool IsContinuationByte(const std::uint8_t value) noexcept
        {
            return value >= 0x80 && value <= 0xBF;
        }

        [[nodiscard]] bool IsValidUtf8Path(const NrUtf8View view) noexcept
        {
            if (view.size == 0)
            {
                return true;
            }

            if (view.data == nullptr)
            {
                return false;
            }

            const auto* bytes = reinterpret_cast<const std::uint8_t*>(view.data);
            std::size_t index = 0;
            while (index < view.size)
            {
                const std::uint8_t first = bytes[index];
                if (first == 0)
                {
                    return false;
                }

                if (first <= 0x7F)
                {
                    ++index;
                    continue;
                }

                if (first >= 0xC2 && first <= 0xDF)
                {
                    if (index + 1 >= view.size || !IsContinuationByte(bytes[index + 1]))
                    {
                        return false;
                    }

                    index += 2;
                    continue;
                }

                if (first >= 0xE0 && first <= 0xEF)
                {
                    if (index + 2 >= view.size)
                    {
                        return false;
                    }

                    const std::uint8_t second = bytes[index + 1];
                    const std::uint8_t third = bytes[index + 2];
                    const bool validSecond = first == 0xE0   ? second >= 0xA0 && second <= 0xBF
                                             : first == 0xED ? second >= 0x80 && second <= 0x9F
                                                             : IsContinuationByte(second);
                    if (!validSecond || !IsContinuationByte(third))
                    {
                        return false;
                    }

                    index += 3;
                    continue;
                }

                if (first >= 0xF0 && first <= 0xF4)
                {
                    if (index + 3 >= view.size)
                    {
                        return false;
                    }

                    const std::uint8_t second = bytes[index + 1];
                    const bool validSecond = first == 0xF0   ? second >= 0x90 && second <= 0xBF
                                             : first == 0xF4 ? second >= 0x80 && second <= 0x8F
                                                             : IsContinuationByte(second);
                    if (!validSecond || !IsContinuationByte(bytes[index + 2]) || !IsContinuationByte(bytes[index + 3]))
                    {
                        return false;
                    }

                    index += 4;
                    continue;
                }

                return false;
            }

            return true;
        }

        [[nodiscard]] NrResult<NrDiagnosticsConfigInternal> InvalidConfig() noexcept
        {
            return NrResult<NrDiagnosticsConfigInternal>::Failure(NrErrorCode::InvalidArgument);
        }
    } // namespace

    NrResult<NrDiagnosticsConfigInternal> CreateDiagnosticsConfigInternal(const NrDiagnosticsConfig& config) noexcept
    {
        if (config.outputPath.size > 0 && config.outputPath.data == nullptr)
        {
            return InvalidConfig();
        }

        NrDiagnosticsConfigInternal internalConfig;
        internalConfig.mode = config.mode;

        switch (config.mode)
        {
        case NrDiagnosticsMode::Disabled:
        case NrDiagnosticsMode::Debug:
            return config.outputPath.size == 0 ? NrResult<NrDiagnosticsConfigInternal>(std::move(internalConfig))
                                               : InvalidConfig();

        case NrDiagnosticsMode::Benchmark:
            if (config.outputPath.size == 0 || !IsValidUtf8Path(config.outputPath))
            {
                return InvalidConfig();
            }

            try
            {
                internalConfig.outputPath.assign(config.outputPath.data, config.outputPath.size);
            }
            catch (const std::bad_alloc&)
            {
                return NrResult<NrDiagnosticsConfigInternal>::Failure(NrErrorCode::OutOfMemory);
            }
            catch (const std::length_error&)
            {
                return NrResult<NrDiagnosticsConfigInternal>::Failure(NrErrorCode::CapacityExceeded);
            }

            return NrResult<NrDiagnosticsConfigInternal>(std::move(internalConfig));
        }

        return InvalidConfig();
    }
} // namespace psnr::runtime::internal
