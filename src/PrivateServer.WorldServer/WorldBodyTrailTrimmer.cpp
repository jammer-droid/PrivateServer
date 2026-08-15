#include "pch.h"

#include "WorldBodyTrailTrimmer.h"

#include <cmath>
#include <cstddef>

namespace psnr::world
{
    // 목표 길이를 초과하지 않도록 tail 쪽 경로 잘름
    WorldBodyTrailTrimResult WorldBodyTrailTrimmer::Trim(const float committedHeadX, const float committedHeadY,
                                                         const float nominalLength,
                                                         BodyTrailComponent& bodyTrail) noexcept
    {
        if (!std::isfinite(committedHeadX) || !std::isfinite(committedHeadY) || !std::isfinite(nominalLength) ||
            nominalLength <= 0.0f)
        {
            return WorldBodyTrailTrimResult::InvalidInput;
        }
        for (std::size_t index = 0; index < bodyTrail.SampleCount(); ++index)
        {
            BodyTrailSample sample;
            if (!bodyTrail.TryRead(index, &sample))
            {
                return WorldBodyTrailTrimResult::BufferInvariantViolation;
            }
            if (!std::isfinite(sample.positionX) || !std::isfinite(sample.positionY))
            {
                return WorldBodyTrailTrimResult::InvalidInput;
            }
        }

        // head -> sample 0 -> sample 1 -> ... -> tail
        // 각 segment 길이 차감
        // nominalLength 가 끝나는 지점 보간
        // 뒤쪽 sample 전부 제거

        float segmentStartX = committedHeadX;
        float segmentStartY = committedHeadY;
        float remainingLength = nominalLength; // 초기 전체 길이
        for (std::size_t index = 0; index < bodyTrail.SampleCount(); ++index)
        {
            // 선분 길이 계산
            BodyTrailSample segmentEnd;
            if (!bodyTrail.TryRead(index, &segmentEnd))
            {
                return WorldBodyTrailTrimResult::BufferInvariantViolation;
            }
            const float deltaX = segmentEnd.positionX - segmentStartX;
            const float deltaY = segmentEnd.positionY - segmentStartY;
            const float segmentLength = std::hypot(deltaX, deltaY); // sqrt(x^2 + y^2)
            if (!std::isfinite(deltaX) || !std::isfinite(deltaY) || !std::isfinite(segmentLength))
            {
                return WorldBodyTrailTrimResult::InvalidInput;
            }
            if (segmentLength == 0.0f)
            {
                segmentStartX = segmentEnd.positionX;
                segmentStartY = segmentEnd.positionY;
                continue;
            }

            // 목표 길이가 현재 선분 안에서 끝나는 경우
            // tail 이 해당 선분 안에 있음
            if (segmentLength >= remainingLength)
            {
                // 1. Tail 이 현재 segment 중간에 있는 경우
                // 2. Tail 이 sample 과 정확히 일치하지만 뒤에 sample 이 더 있음
                const bool hasExcessTrail = segmentLength > remainingLength || index + 1 < bodyTrail.SampleCount();
                if (!hasExcessTrail)
                {
                    return WorldBodyTrailTrimResult::WithinLength;
                }

                // 보간 비율 계산
                const float tailRatio = remainingLength / segmentLength;
                // 보간된 endpoint 뒤의 sample 은 logical count만 줄여 제거
                if (!bodyTrail.TryWrite(index,
                                        BodyTrailSample{
                                            segmentStartX + deltaX * tailRatio,
                                            segmentStartY + deltaY * tailRatio,
                                        }) ||
                    !bodyTrail.TryTrimBack(index + 1))
                {
                    return WorldBodyTrailTrimResult::BufferInvariantViolation;
                }
                return WorldBodyTrailTrimResult::Trimmed;
            }

            // 현재 선분을 전부 사용할 수 있는 경우
            remainingLength -= segmentLength;
            segmentStartX = segmentEnd.positionX;
            segmentStartY = segmentEnd.positionY;
        }

        return WorldBodyTrailTrimResult::WithinLength;
    }
} // namespace psnr::world
