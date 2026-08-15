#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace psnr::core::utils
{
    [[nodiscard]] constexpr bool NrIsPowerOfTwo(std::size_t value) noexcept
    {
        return value != 0 && (value & (value - 1)) == 0;
    }

    [[nodiscard]] constexpr bool NrTryMultiply(std::size_t left, std::size_t right, std::size_t& result) noexcept
    {
        if (left != 0 && right > (std::numeric_limits<std::size_t>::max() / left))
        {
            return false;
        }

        result = left * right;
        return true;
    }

    // value 를 alignment 배수로 올려 aligned를 결정
    // alignment는 2의 거듭제곱
    [[nodiscard]] constexpr bool NrTryAlignUp(std::size_t value, std::size_t alignment, std::size_t& aligned) noexcept
    {
        const std::size_t remainder =
            value & (alignment - 1); // value % alignment와 동일. 나머지가 0이면 value는 이미 정렬된 상태
        if (remainder == 0)
        {
            aligned = value;
            return true;
        }

        const std::size_t padding = alignment - remainder;
        if (value > std::numeric_limits<std::size_t>::max() - padding)
        {
            return false;
        }

        aligned = value + padding;
        return true;
    }

    [[nodiscard]] constexpr bool NrIsAligned(std::uintptr_t address, std::size_t alignment) noexcept
    {
        return alignment != 0 && (address % alignment) == 0;
    }

    // 같은 정의가 있어도 하나로 취급하기 위해 inline 사용 + runtime(reinterpret_cast) 주소 계산
    // constexpr은 암묵적으로 inline 취급
    [[nodiscard]] inline bool NrIsAligned(const void* address, std::size_t alignment) noexcept
    {
        return NrIsAligned(reinterpret_cast<std::uintptr_t>(address), alignment);
    }
} // namespace psnr::core::utils
