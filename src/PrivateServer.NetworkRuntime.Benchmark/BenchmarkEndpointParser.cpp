#include "BenchmarkEndpointParser.h"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <system_error>

namespace psnr::benchmark
{
    bool BenchmarkEndpointParser::TryParseServerEndpoint(const BenchmarkServerConfigV1& config,
                                                         psnr::runtime::NrEndpoint* const outEndpoint) noexcept
    {
        return TryParseServerEndpoint(config.address, config.port, outEndpoint);
    }

    bool BenchmarkEndpointParser::TryParseServerEndpoint(const std::string_view address, const std::uint16_t port,
                                                         psnr::runtime::NrEndpoint* const outEndpoint) noexcept
    {
        if (outEndpoint == nullptr || port == 0)
        {
            return false;
        }

        std::uint8_t octets[4]{};
        std::size_t componentBegin = 0;
        for (std::size_t index = 0; index < 4; ++index)
        {
            const std::size_t componentEnd = address.find('.', componentBegin);
            const bool isLastComponent = index == 3;
            if ((!isLastComponent && componentEnd == std::string_view::npos) ||
                (isLastComponent && componentEnd != std::string_view::npos))
            {
                return false;
            }

            const std::size_t parseEnd = isLastComponent ? address.size() : componentEnd;
            if (componentBegin == parseEnd)
            {
                return false;
            }

            std::uint32_t value = 0;
            const char* const begin = address.data() + componentBegin;
            const char* const end = address.data() + parseEnd;
            const std::from_chars_result parseResult = std::from_chars(begin, end, value);
            if (parseResult.ec != std::errc{} || parseResult.ptr != end || value > 255)
            {
                return false;
            }

            octets[index] = static_cast<std::uint8_t>(value);
            componentBegin = parseEnd + 1;
        }

        psnr::runtime::NrEndpoint endpoint;
        endpoint.ipv4Address = psnr::runtime::NrIPv4Address{octets[0], octets[1], octets[2], octets[3]};
        endpoint.port = port;
        *outEndpoint = endpoint;
        return true;
    }
} // namespace psnr::benchmark
