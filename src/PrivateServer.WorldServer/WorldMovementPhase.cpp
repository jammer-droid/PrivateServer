#include "pch.h"

#include "WorldMovementPhase.h"

#include <cmath>
#include <cstddef>

namespace psnr::world
{
    WorldMovementPhaseComputeResult WorldMovementPhase::Compute(
        const std::span<const WorldMovementTickInput> inputs, const float fixedDeltaSeconds,
        const WorldReadView& readView, const std::span<WorldMovementEntityUpdate> outUpdates) noexcept
    {
        if (!std::isfinite(fixedDeltaSeconds) || fixedDeltaSeconds <= 0.0f || inputs.size() != outUpdates.size())
        {
            return WorldMovementPhaseComputeResult::InvalidArgument;
        }

        for (std::size_t index = 0; index < inputs.size(); ++index)
        {
            const WorldMovementTickInput& input = inputs[index];
            WorldEntityComponents components;
            if (!readView.TryReadComponents(input.entityKey, &components))
            {
                return WorldMovementPhaseComputeResult::EntityStateInvariantViolation;
            }

            const float moveSpeed =
                input.usesControlMovement ? input.moveSpeed : components.movementCapability.maxMoveSpeed;
            if (input.usesControlMovement)
            {
                components.transform.angleRadians = input.headingRadians;
            }
            components.motion.movementIntentX = input.movementInputX;
            components.motion.movementIntentY = input.movementInputY;
            components.motion.velocityX = input.movementInputX * moveSpeed;
            components.motion.velocityY = input.movementInputY * moveSpeed;
            components.transform.positionX += components.motion.velocityX * fixedDeltaSeconds;
            components.transform.positionY += components.motion.velocityY * fixedDeltaSeconds;

            outUpdates[index] = WorldMovementEntityUpdate{
                input.entityKey,
                components.transform,
                components.motion,
            };
        }

        return WorldMovementPhaseComputeResult::Computed;
    }
} // namespace psnr::world
