#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace psnr::logging
{
    // 로그 한 건이 어떤 tick/session/entity 작업과 연관이 있는지
    struct ApplicationLogContext final
    {
        std::optional<std::uint64_t> serverTick;
        std::optional<std::uint64_t> epoch;
        std::optional<std::uint64_t> worldSessionKey;
        std::optional<std::uint32_t> entityId;
        std::optional<std::uint32_t> entityGeneration;
        std::optional<std::string> operation;
        std::optional<std::string> result;
        std::optional<std::string> error;
        std::optional<std::uint32_t> nativeErrorCode;
    };
} // namespace psnr::logging
