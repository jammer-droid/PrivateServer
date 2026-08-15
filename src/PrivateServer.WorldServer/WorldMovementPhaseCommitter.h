#pragma once

#include "WorldEntityManager.h"
#include "WorldMovementPhaseResult.h"

namespace psnr::world
{
    // Movement phase 결과를 canonical entity state에 반영하는 World owner 전용 경계다.
    class WorldMovementPhaseCommitter final
    {
    public:
        static void Commit(const WorldMovementPhaseResult& result, WorldEntityManager& entityManager);
    };
} // namespace psnr::world
