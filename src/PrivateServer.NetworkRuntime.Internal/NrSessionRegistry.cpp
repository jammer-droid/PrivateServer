#include "pch.h"

#include "NrSessionRegistry.h"

#include "NrErrorCode.h"

#include <new>
#include <utility>

namespace psnr::runtime
{
    using psnr::core::NrErrorCode;

    NrStatus NrSessionRegistry::TryRegister(NrSession&& session) noexcept
    {
        if (!session.IsValid())
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        const NrSessionKey sessionKey = session.SessionKey();
        if (sessions_.find(sessionKey) != sessions_.end())
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        try
        {
            sessions_.emplace(sessionKey, std::move(session));
        }
        catch (const std::bad_alloc&)
        {
            return NrStatus::Failure(NrErrorCode::OutOfMemory);
        }

        return NrStatus::Success();
    }

    NrSession* NrSessionRegistry::Find(NrSessionKey sessionKey) noexcept
    {
        NrSessionMap::iterator iterator = sessions_.find(sessionKey);
        if (iterator == sessions_.end())
        {
            return nullptr;
        }

        return &iterator->second;
    }

    const NrSession* NrSessionRegistry::Find(NrSessionKey sessionKey) const noexcept
    {
        NrSessionMap::const_iterator iterator = sessions_.find(sessionKey);
        if (iterator == sessions_.end())
        {
            return nullptr;
        }

        return &iterator->second;
    }

    NrResult<NrSession> NrSessionRegistry::Remove(NrSessionKey sessionKey) noexcept
    {
        NrSessionMap::iterator iterator = sessions_.find(sessionKey);
        if (iterator == sessions_.end())
        {
            return NrResult<NrSession>::Failure(NrErrorCode::InvalidArgument);
        }

        NrSession session = std::move(iterator->second);
        sessions_.erase(iterator);

        return NrResult<NrSession>(std::move(session));
    }

    std::size_t NrSessionRegistry::Count() const noexcept
    {
        return sessions_.size();
    }
} // namespace psnr::runtime
