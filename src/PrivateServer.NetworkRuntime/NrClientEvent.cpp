#include "pch.h"

#include "NrClientEvent.h"

#include "NrClientEventAccess.h"
#include "NrErrorCode.h"

#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <utility>

namespace psnr::runtime
{
    using psnr::core::NrErrorCode;

    struct NrClientEvent::Impl
    {
        NrClientEventKind kind = NrClientEventKind::None;
        NrPacketType packetType{};
        std::unique_ptr<std::byte[]> payload;
        std::uint32_t payloadLength = 0;
        NrStatus transportStatus;
        NrClientDisconnectReason disconnectReason = NrClientDisconnectReason::None;
    };

    NrClientEvent::NrClientEvent() noexcept = default;

    NrClientEvent::NrClientEvent(NrClientEvent&& other) noexcept
        : impl_(std::exchange(other.impl_, nullptr))
    {
    }

    NrClientEvent& NrClientEvent::operator=(NrClientEvent&& other) noexcept
    {
        if (this != &other)
        {
            delete impl_;
            impl_ = std::exchange(other.impl_, nullptr);
        }

        return *this;
    }

    NrClientEvent::~NrClientEvent() noexcept
    {
        delete impl_;
        impl_ = nullptr;
    }

    bool NrClientEvent::IsValid() const noexcept
    {
        return impl_ != nullptr;
    }

    NrClientEventKind NrClientEvent::Kind() const noexcept
    {
        return impl_ == nullptr ? NrClientEventKind::None : impl_->kind;
    }

    NrStatus NrClientEvent::GetPacketType(NrPacketType* outPacketType) const noexcept
    {
        if (outPacketType == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        if (impl_ == nullptr || impl_->kind != NrClientEventKind::PacketReceived)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        *outPacketType = impl_->packetType;
        return NrStatus::Success();
    }

    NrStatus NrClientEvent::GetPayload(NrByteView* outPayload) const noexcept
    {
        if (outPayload == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        if (impl_ == nullptr || impl_->kind != NrClientEventKind::PacketReceived)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        *outPayload = NrByteView{impl_->payload.get(), impl_->payloadLength};
        return NrStatus::Success();
    }

    NrStatus NrClientEvent::GetTransportStatus(NrStatus* outStatus) const noexcept
    {
        if (outStatus == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        if (impl_ == nullptr || (impl_->kind != NrClientEventKind::TransportConnectionFailed &&
                                 impl_->kind != NrClientEventKind::TransportDisconnected))
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        *outStatus = impl_->transportStatus;
        return NrStatus::Success();
    }

    NrStatus NrClientEvent::GetDisconnectReason(NrClientDisconnectReason* outReason) const noexcept
    {
        if (outReason == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        if (impl_ == nullptr || impl_->kind != NrClientEventKind::TransportDisconnected)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        *outReason = impl_->disconnectReason;
        return NrStatus::Success();
    }

    NrClientEvent::NrClientEvent(Impl* impl) noexcept
        : impl_(impl)
    {
    }

    NrStatus internal::NrClientEventAccess::CreateTransportConnected(NrClientEvent& outEvent) noexcept
    {
        if (outEvent.IsValid())
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        NrClientEvent::Impl* impl = new (std::nothrow) NrClientEvent::Impl();
        if (impl == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::OutOfMemory);
        }

        impl->kind = NrClientEventKind::TransportConnected;
        outEvent = NrClientEvent(impl);
        return NrStatus::Success();
    }

    NrStatus internal::NrClientEventAccess::CreateTransportConnectionFailed(const NrStatus transportStatus,
                                                                            NrClientEvent& outEvent) noexcept
    {
        if (transportStatus.Succeeded())
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        if (outEvent.IsValid())
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        NrClientEvent::Impl* impl = new (std::nothrow) NrClientEvent::Impl();
        if (impl == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::OutOfMemory);
        }

        impl->kind = NrClientEventKind::TransportConnectionFailed;
        impl->transportStatus = transportStatus;
        outEvent = NrClientEvent(impl);
        return NrStatus::Success();
    }

    NrStatus internal::NrClientEventAccess::CreatePacketReceived(const NrPacketType packetType,
                                                                 const std::span<const std::byte> payload,
                                                                 NrClientEvent& outEvent) noexcept
    {
        if (payload.size() > std::numeric_limits<std::uint32_t>::max())
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        if (outEvent.IsValid())
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        NrClientEvent::Impl* impl = new (std::nothrow) NrClientEvent::Impl();
        if (impl == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::OutOfMemory);
        }

        if (!payload.empty())
        {
            impl->payload.reset(new (std::nothrow) std::byte[payload.size()]);
            if (impl->payload == nullptr)
            {
                delete impl;
                return NrStatus::Failure(NrErrorCode::OutOfMemory);
            }

            std::memcpy(impl->payload.get(), payload.data(), payload.size());
        }

        impl->kind = NrClientEventKind::PacketReceived;
        impl->packetType = packetType;
        impl->payloadLength = static_cast<std::uint32_t>(payload.size());
        outEvent = NrClientEvent(impl);
        return NrStatus::Success();
    }

    NrStatus internal::NrClientEventAccess::CreateTransportDisconnected(const NrClientDisconnectReason reason,
                                                                        const NrStatus transportStatus,
                                                                        NrClientEvent& outEvent) noexcept
    {
        if (reason == NrClientDisconnectReason::None)
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        if (outEvent.IsValid())
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        NrClientEvent::Impl* impl = new (std::nothrow) NrClientEvent::Impl();
        if (impl == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::OutOfMemory);
        }

        impl->kind = NrClientEventKind::TransportDisconnected;
        impl->transportStatus = transportStatus;
        impl->disconnectReason = reason;
        outEvent = NrClientEvent(impl);
        return NrStatus::Success();
    }
} // namespace psnr::runtime
