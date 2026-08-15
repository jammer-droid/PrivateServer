#pragma once

#include "WorldResult.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace psnr::world
{
    // 뱀 몸통 중심선을 구성하기 위한 과거의 머리 위치
    struct BodyTrailSample final
    {
        float positionX = 0.0f;
        float positionY = 0.0f;

        [[nodiscard]] friend bool operator==(const BodyTrailSample& left,
                                             const BodyTrailSample& right) noexcept = default;
    };

    class BodyTrailComponent final
    {
    public:
        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] std::size_t MaxSampleCount() const noexcept;
        [[nodiscard]] std::size_t SampleCount() const noexcept;
        [[nodiscard]] bool Empty() const noexcept;

        [[nodiscard]] bool TryPushFront(BodyTrailSample sample) noexcept;
        [[nodiscard]] bool TryPushBack(BodyTrailSample sample) noexcept;
        [[nodiscard]] bool TryRead(std::size_t logicalIndex, BodyTrailSample* outSample) const noexcept;
        [[nodiscard]] bool TryWrite(std::size_t logicalIndex, BodyTrailSample sample) noexcept;
        [[nodiscard]] bool TryTrimBack(std::size_t retainedSampleCount) noexcept;

        [[nodiscard]] friend bool operator==(const BodyTrailComponent& left, const BodyTrailComponent& right) noexcept;

    private:
        friend WorldResult<BodyTrailComponent> CreateBodyTrailComponent(std::uint32_t maxSampleCount) noexcept;

        [[nodiscard]] std::size_t PhysicalIndex(std::size_t logicalIndex) const noexcept;

        std::vector<BodyTrailSample> storage_;
        std::size_t frontIndex_ = 0;
        std::size_t sampleCount_ = 0;
    };

    [[nodiscard]] WorldResult<BodyTrailComponent> CreateBodyTrailComponent(std::uint32_t maxSampleCount) noexcept;
} // namespace psnr::world
