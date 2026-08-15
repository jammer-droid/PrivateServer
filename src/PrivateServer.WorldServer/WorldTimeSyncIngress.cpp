#include "pch.h"

#include "WorldTimeSyncIngress.h"

#include "WorldTimeSyncRequest.h"

namespace psnr::world
{
    WorldIngressAdmissionResult WorldTimeSyncIngress::Admit(
        const WorldIngressAdmissionContext& context, const std::uint32_t lastCompletedServerTick,
        const std::span<const std::byte> payload, protocol::v1::WorldTimeSyncResponse* const outResponse) noexcept
    {
        if (outResponse == nullptr)
        {
            return WorldIngressAdmissionResult::InvalidArgument;
        }

        protocol::v1::WorldTimeSyncRequest request;
        if (protocol::v1::WorldTimeSyncRequest::Decode(payload, &request) != protocol::WorldProtocolError::Success)
        {
            return WorldIngressAdmissionResult::MalformedPayload;
        }

        WorldSession session;
        if (!context.sessionRegistry.TryFind(context.sessionKey, &session))
        {
            return WorldIngressAdmissionResult::SessionNotFound;
        }
        if (!session.IsJoined())
        {
            return WorldIngressAdmissionResult::SessionNotJoined;
        }

        const protocol::v1::WorldTimeSyncResponse response{
            request.probeSequence,
            lastCompletedServerTick,
        };
        *outResponse = response;
        return WorldIngressAdmissionResult::Accepted;
    }
} // namespace psnr::world
