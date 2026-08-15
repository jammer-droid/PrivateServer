#include "pch.h"

#include "NrInputFactory.h"

#include "NrMemoryPoolManager.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace psnr::core
{
    namespace
    {
        [[nodiscard]] bool IsKnownDispatchLane(NrDispatchLane dispatchLane) noexcept
        {
            return static_cast<std::size_t>(dispatchLane) < static_cast<std::size_t>(NrDispatchLane::Count);
        }
    } // namespace

    NrInputFactory::NrInputFactory(NrMemoryPoolManager& memoryPoolManager) noexcept
        : memoryPoolManager_(memoryPoolManager)
    {
    }

    NrResult<NrInput> NrInputFactory::CreateInput(NrSessionKey sessionId, const NrPacketParseResult& parseResult,
                                                  const NrPacketDispatchRule& dispatchRule) noexcept
    {
        if (parseResult.status != NrPacketParseStatus::Complete)
        {
            return NrResult<NrInput>::Failure(NrErrorCode::InvalidArgument);
        }

        if (!IsKnownDispatchLane(dispatchRule.dispatchLane))
        {
            return NrResult<NrInput>::Failure(NrErrorCode::InvalidArgument);
        }

        if (parseResult.header.packetType != dispatchRule.packetType.value)
        {
            return NrResult<NrInput>::Failure(NrErrorCode::InvalidArgument);
        }

        if (parseResult.packetBytes.size() < NrPacketHeaderLength ||
            parseResult.packetBytes.size() != parseResult.header.packetLength)
        {
            return NrResult<NrInput>::Failure(NrErrorCode::InvalidArgument);
        }

        const std::span<const std::byte> payloadBytes = parseResult.packetBytes.subspan(NrPacketHeaderLength);
        NrResult<NrPayload> payloadResult = NrPayloadFactory::CreatePayloadFrom(memoryPoolManager_, payloadBytes);
        if (payloadResult.Failed())
        {
            return NrResult<NrInput>::Failure(payloadResult.Status());
        }

        NrPayload payload = payloadResult.TakeValue();
        return NrResult<NrInput>(
            NrInput(sessionId, dispatchRule.packetType, dispatchRule.dispatchLane, std::move(payload)));
    }
} // namespace psnr::core
