#include "pch.h"

#include "WorldDeathDropAnchorPlanner.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace psnr::world
{
    WorldResult<std::vector<WorldDeathDropAnchor>> WorldDeathDropAnchorPlanner::Plan(
        const float headPositionX, const float headPositionY, const BodyTrailComponent& bodyTrail,
        const std::uint32_t growthPoint) noexcept
    {
        if (!std::isfinite(headPositionX) || !std::isfinite(headPositionY) || !bodyTrail.IsInitialized() ||
            bodyTrail.Empty())
        {
            return WorldResult<std::vector<WorldDeathDropAnchor>>::Failure(WorldErrorCode::InvalidInput);
        }

        try
        {
            const std::size_t sampleCount = bodyTrail.SampleCount();
            std::vector<BodyTrailSample> samples;
            std::vector<float> segmentLengths;
            samples.reserve(sampleCount);
            segmentLengths.reserve(sampleCount);

            // BodyTrailSample 개수는 growth point가 아니라 tick 기반 sampling 주기에 따라 달라진다.
            // 따라서 sample마다 drop을 할당하지 않고, committed head부터 tail까지 실제 polyline 길이를 누적한다.
            float previousX = headPositionX;
            float previousY = headPositionY;
            float totalLength = 0.0f;
            for (std::size_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex)
            {
                BodyTrailSample sample;
                if (!bodyTrail.TryRead(sampleIndex, &sample) || !std::isfinite(sample.positionX) ||
                    !std::isfinite(sample.positionY))
                {
                    return WorldResult<std::vector<WorldDeathDropAnchor>>::Failure(WorldErrorCode::InvalidInput);
                }
                const float segmentLength = std::hypot(sample.positionX - previousX, sample.positionY - previousY);
                if (!std::isfinite(segmentLength) || !std::isfinite(totalLength + segmentLength))
                {
                    return WorldResult<std::vector<WorldDeathDropAnchor>>::Failure(WorldErrorCode::InvalidInput);
                }
                samples.push_back(sample);
                segmentLengths.push_back(segmentLength);
                totalLength += segmentLength;
                previousX = sample.positionX;
                previousY = sample.positionY;
            }

            std::vector<WorldDeathDropAnchor> anchors;
            anchors.reserve(growthPoint);
            if (growthPoint == 0)
            {
                return WorldResult<std::vector<WorldDeathDropAnchor>>(std::move(anchors));
            }
            if (totalLength <= 0.0f)
            {
                return WorldResult<std::vector<WorldDeathDropAnchor>>::Failure(WorldErrorCode::InvalidInput);
            }

            std::size_t segmentIndex = 0;
            float distanceBeforeSegment = 0.0f;
            for (std::uint32_t dropOrdinal = 0; dropOrdinal < growthPoint; ++dropOrdinal)
            {
                // Body 전체를 growthPoint개의 동일한 길이 구간으로 나누고 각 구간 중앙에 하나씩 배치한다.
                // 0.0과 1.0 끝점을 피하므로 drop이 committed head나 tail 끝에 직접 겹치지 않는다.
                const float normalizedDistance =
                    (static_cast<float>(dropOrdinal) + 0.5f) / static_cast<float>(growthPoint);
                const float targetDistance = totalLength * normalizedDistance;
                while (segmentIndex < sampleCount &&
                       distanceBeforeSegment + segmentLengths[segmentIndex] <= targetDistance)
                {
                    distanceBeforeSegment += segmentLengths[segmentIndex];
                    ++segmentIndex;
                }
                if (segmentIndex >= sampleCount || segmentLengths[segmentIndex] <= 0.0f)
                {
                    return WorldResult<std::vector<WorldDeathDropAnchor>>::Failure(WorldErrorCode::InvalidInput);
                }

                // 누적 target distance가 포함된 선분 내부의 비율을 구해 실제 world 좌표로 보간한다.
                const BodyTrailSample segmentStart =
                    segmentIndex == 0 ? BodyTrailSample{headPositionX, headPositionY} : samples[segmentIndex - 1];
                const BodyTrailSample& segmentEnd = samples[segmentIndex];
                const float amount = (targetDistance - distanceBeforeSegment) / segmentLengths[segmentIndex];
                const float positionX =
                    segmentStart.positionX + (segmentEnd.positionX - segmentStart.positionX) * amount;
                const float positionY =
                    segmentStart.positionY + (segmentEnd.positionY - segmentStart.positionY) * amount;
                if (!std::isfinite(positionX) || !std::isfinite(positionY))
                {
                    return WorldResult<std::vector<WorldDeathDropAnchor>>::Failure(WorldErrorCode::InvalidInput);
                }
                anchors.push_back(WorldDeathDropAnchor{dropOrdinal, positionX, positionY});
            }

            return WorldResult<std::vector<WorldDeathDropAnchor>>(std::move(anchors));
        }
        catch (...)
        {
            return WorldResult<std::vector<WorldDeathDropAnchor>>::Failure(WorldErrorCode::AllocationFailed);
        }
    }
} // namespace psnr::world
