#include "pch.h"

#include "WorldCollisionDeathResolver.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace psnr::world
{
    namespace
    {
        [[nodiscard]] bool IsValid(const WorldCollisionProxyKey& key) noexcept
        {
            const bool knownRole =
                key.role == WorldCollisionProxyRole::PlayerHead || key.role == WorldCollisionProxyRole::PlayerBody;
            const bool validHead = key.role != WorldCollisionProxyRole::PlayerHead || key.segmentOrdinal == 0;
            return key.ownerKey.IsValid() && knownRole && validHead;
        }
    } // namespace

    WorldResult<std::vector<WorldEntityKey>> WorldCollisionDeathResolver::Resolve(
        const std::span<const WorldCollisionContact> contacts) noexcept
    {
        for (const WorldCollisionContact& contact : contacts)
        {
            if (!IsValid(contact.first) || !IsValid(contact.second) ||
                (contact.first.role == WorldCollisionProxyRole::PlayerBody &&
                 contact.second.role == WorldCollisionProxyRole::PlayerBody))
            {
                return WorldResult<std::vector<WorldEntityKey>>::Failure(WorldErrorCode::InvalidInput);
            }
        }

        try
        {
            std::vector<WorldEntityKey> deathSet;
            if (contacts.size() > deathSet.max_size() / 2)
            {
                return WorldResult<std::vector<WorldEntityKey>>::Failure(WorldErrorCode::AllocationFailed);
            }
            deathSet.reserve(contacts.size() * 2);

            for (const WorldCollisionContact& contact : contacts)
            {
                if (contact.first.ownerKey == contact.second.ownerKey)
                {
                    continue;
                }
                if (contact.first.role == WorldCollisionProxyRole::PlayerHead)
                {
                    deathSet.push_back(contact.first.ownerKey);
                }
                if (contact.second.role == WorldCollisionProxyRole::PlayerHead)
                {
                    deathSet.push_back(contact.second.ownerKey);
                }
            }

            std::sort(deathSet.begin(), deathSet.end());
            deathSet.erase(std::unique(deathSet.begin(), deathSet.end()), deathSet.end());
            return WorldResult<std::vector<WorldEntityKey>>(std::move(deathSet));
        }
        catch (...)
        {
            return WorldResult<std::vector<WorldEntityKey>>::Failure(WorldErrorCode::AllocationFailed);
        }
    }
} // namespace psnr::world
