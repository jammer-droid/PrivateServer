#include "BenchmarkClientTransport.h"

#include <PrivateServer/NetworkRuntime/NrErrorCode.h>

#include <chrono>
#include <string>
#include <thread>
#include <utility>

namespace psnr::benchmark
{
    namespace
    {
        constexpr std::chrono::milliseconds EventPollInterval{1};
    }

    std::string BenchmarkClientTransport::DescribeFailure(const std::string_view operation,
                                                          const psnr::core::NrStatus status)
    {
        std::string error{operation};
        error.append(" failed with errorCode=");
        error.append(std::to_string(static_cast<int>(status.ErrorCode())));
        error.append(" nativeErrorCode=");
        error.append(std::to_string(status.NativeErrorCode()));
        return error;
    }

    std::string BenchmarkClientTransport::ReadNextEventUntil(psnr::runtime::NrClient& client,
                                                             const std::chrono::steady_clock::time_point deadline,
                                                             psnr::runtime::NrClientEvent* const outEvent)
    {
        if (outEvent == nullptr)
        {
            return "NrClient event output is required";
        }

        while (std::chrono::steady_clock::now() < deadline)
        {
            bool eventRead = false;
            const std::string readError = TryReadNextEvent(client, outEvent, &eventRead);
            if (!readError.empty())
            {
                return readError;
            }
            if (eventRead)
            {
                return {};
            }
            std::this_thread::sleep_for(EventPollInterval);
        }

        return "NrClient event wait timed out";
    }

    std::string BenchmarkClientTransport::TryReadNextEvent(psnr::runtime::NrClient& client,
                                                           psnr::runtime::NrClientEvent* const outEvent,
                                                           bool* const outEventRead)
    {
        if (outEvent == nullptr || outEventRead == nullptr)
        {
            return "NrClient event outputs are required";
        }

        *outEventRead = false;
        psnr::runtime::NrClientEvent event;
        const psnr::core::NrStatus popStatus = client.TryPopEvent(&event);
        if (popStatus.ErrorCode() == psnr::core::NrErrorCode::QueueEmpty)
        {
            return {};
        }
        if (popStatus.Failed())
        {
            return DescribeFailure("NrClient::TryPopEvent", popStatus);
        }

        *outEvent = std::move(event);
        *outEventRead = true;
        return {};
    }
} // namespace psnr::benchmark
