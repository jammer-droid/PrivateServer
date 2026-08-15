#pragma once

#include "ApplicationLogSeverity.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace psnr::logging::internal
{
    inline constexpr std::size_t CanonicalRunIdLength = 61;
    inline constexpr std::size_t RunIdUuidOffset = 25;

    [[nodiscard]] inline bool IsDecimalDigit(const char value) noexcept
    {
        return value >= '0' && value <= '9';
    }

    [[nodiscard]] inline bool IsLowercaseLetter(const char value) noexcept
    {
        return value >= 'a' && value <= 'z';
    }

    [[nodiscard]] inline bool IsUtf8ContinuationByte(const unsigned char value) noexcept
    {
        return (value & 0xC0U) == 0x80U;
    }

    [[nodiscard]] inline std::size_t Utf8PrefixSizeWithinLimit(const std::string_view value,
                                                               const std::size_t maximumBytes) noexcept
    {
        if (value.size() <= maximumBytes)
        {
            return value.size();
        }

        std::size_t prefixSize = maximumBytes;
        while (prefixSize > 0 &&
               IsUtf8ContinuationByte(static_cast<unsigned char>(value[prefixSize])))
        {
            --prefixSize;
        }

        return prefixSize;
    }

    [[nodiscard]] inline bool IsValidUtf8(const std::string_view value) noexcept
    {
        std::size_t index = 0;
        while (index < value.size())
        {
            const unsigned char first = static_cast<unsigned char>(value[index]);
            if (first <= 0x7FU)
            {
                ++index;
                continue;
            }

            if (first >= 0xC2U && first <= 0xDFU)
            {
                if (index + 1 >= value.size() ||
                    !IsUtf8ContinuationByte(static_cast<unsigned char>(value[index + 1])))
                {
                    return false;
                }

                index += 2;
                continue;
            }

            if (first >= 0xE0U && first <= 0xEFU)
            {
                if (index + 2 >= value.size())
                {
                    return false;
                }

                const unsigned char second = static_cast<unsigned char>(value[index + 1]);
                const unsigned char third = static_cast<unsigned char>(value[index + 2]);
                bool validSecond = IsUtf8ContinuationByte(second);
                if (first == 0xE0U)
                {
                    validSecond = second >= 0xA0U && second <= 0xBFU;
                }
                else if (first == 0xEDU)
                {
                    validSecond = second >= 0x80U && second <= 0x9FU;
                }
                if (!validSecond || !IsUtf8ContinuationByte(third))
                {
                    return false;
                }

                index += 3;
                continue;
            }

            if (first >= 0xF0U && first <= 0xF4U)
            {
                if (index + 3 >= value.size())
                {
                    return false;
                }

                const unsigned char second = static_cast<unsigned char>(value[index + 1]);
                const unsigned char third = static_cast<unsigned char>(value[index + 2]);
                const unsigned char fourth = static_cast<unsigned char>(value[index + 3]);
                bool validSecond = IsUtf8ContinuationByte(second);
                if (first == 0xF0U)
                {
                    validSecond = second >= 0x90U && second <= 0xBFU;
                }
                else if (first == 0xF4U)
                {
                    validSecond = second >= 0x80U && second <= 0x8FU;
                }
                if (!validSecond || !IsUtf8ContinuationByte(third) || !IsUtf8ContinuationByte(fourth))
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

    [[nodiscard]] inline bool IsHexDigit(const char value) noexcept
    {
        return IsDecimalDigit(value) || (value >= 'a' && value <= 'f') || (value >= 'A' && value <= 'F');
    }

    [[nodiscard]] inline bool ContainsOnlyDecimalDigits(const std::string_view value) noexcept
    {
        for (const char character : value)
        {
            if (!IsDecimalDigit(character))
            {
                return false;
            }
        }

        return true;
    }

    [[nodiscard]] inline unsigned int DecimalNumber(const std::string_view value) noexcept
    {
        unsigned int result = 0;
        for (const char character : value)
        {
            result = result * 10U + static_cast<unsigned int>(character - '0');
        }

        return result;
    }

    [[nodiscard]] inline bool IsLeapYear(const unsigned int year) noexcept
    {
        return year % 4U == 0U && (year % 100U != 0U || year % 400U == 0U);
    }

    [[nodiscard]] inline unsigned int DaysInMonth(const unsigned int year, const unsigned int month) noexcept
    {
        constexpr unsigned int daysByMonth[] = {31U, 28U, 31U, 30U, 31U, 30U,
                                                 31U, 31U, 30U, 31U, 30U, 31U};
        if (month == 2U && IsLeapYear(year))
        {
            return 29U;
        }

        return daysByMonth[month - 1U];
    }

    [[nodiscard]] inline bool MatchesCanonicalUtcTimestamp(const std::string_view timestamp) noexcept
    {
        if (timestamp.size() != 20 || !ContainsOnlyDecimalDigits(timestamp.substr(0, 8)) || timestamp[8] != 'T' ||
            !ContainsOnlyDecimalDigits(timestamp.substr(9, 6)) || timestamp[15] != '.' ||
            !ContainsOnlyDecimalDigits(timestamp.substr(16, 3)) || timestamp[19] != 'Z')
        {
            return false;
        }

        const unsigned int year = DecimalNumber(timestamp.substr(0, 4));
        const unsigned int month = DecimalNumber(timestamp.substr(4, 2));
        const unsigned int day = DecimalNumber(timestamp.substr(6, 2));
        const unsigned int hour = DecimalNumber(timestamp.substr(9, 2));
        const unsigned int minute = DecimalNumber(timestamp.substr(11, 2));
        const unsigned int second = DecimalNumber(timestamp.substr(13, 2));
        if (year == 0U || month == 0U || month > 12U || day == 0U)
        {
            return false;
        }

        return day <= DaysInMonth(year, month) && hour <= 23U && minute <= 59U && second <= 59U;
    }

    [[nodiscard]] inline bool IsUuidHyphenOffset(const std::size_t offset) noexcept
    {
        return offset == 8 || offset == 13 || offset == 18 || offset == 23;
    }

    [[nodiscard]] inline bool MatchesCanonicalRunId(const std::string_view runId) noexcept
    {
        if (runId.size() != CanonicalRunIdLength || runId.substr(0, 4) != "run-")
        {
            return false;
        }

        if (!MatchesCanonicalUtcTimestamp(runId.substr(4, 20)) || runId[24] != '-')
        {
            return false;
        }

        for (std::size_t index = RunIdUuidOffset; index < runId.size(); ++index)
        {
            const std::size_t uuidOffset = index - RunIdUuidOffset;
            if (IsUuidHyphenOffset(uuidOffset))
            {
                if (runId[index] != '-')
                {
                    return false;
                }
            }
            else if (!IsHexDigit(runId[index]))
            {
                return false;
            }
        }

        return true;
    }

    [[nodiscard]] inline bool IsStableToken(const std::string_view value) noexcept
    {
        if (value.empty() || !IsLowercaseLetter(value.front()) || value.back() == '_')
        {
            return false;
        }

        bool previousWasUnderscore = false;
        for (const char character : value)
        {
            if (character == '_')
            {
                if (previousWasUnderscore)
                {
                    return false;
                }

                previousWasUnderscore = true;
                continue;
            }

            if (!IsLowercaseLetter(character) && !IsDecimalDigit(character))
            {
                return false;
            }

            previousWasUnderscore = false;
        }

        return true;
    }

    [[nodiscard]] inline bool IsValidOptionalToken(const std::optional<std::string>& value) noexcept
    {
        return !value.has_value() || IsStableToken(*value);
    }

    [[nodiscard]] inline bool IsValidSeverity(const ApplicationLogSeverity severity) noexcept
    {
        switch (severity)
        {
        case ApplicationLogSeverity::Debug:
        case ApplicationLogSeverity::Info:
        case ApplicationLogSeverity::Warning:
        case ApplicationLogSeverity::Error:
        case ApplicationLogSeverity::Critical:
            return true;
        default:
            return false;
        }
    }
} // namespace psnr::logging::internal
