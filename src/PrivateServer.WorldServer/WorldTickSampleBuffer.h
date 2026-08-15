#pragma once

#include "WorldResult.h"
#include "WorldTickSample.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace psnr::world
{
    class WorldTickSampleBuffer final
    {
    public:
        [[nodiscard]] static WorldResult<std::unique_ptr<WorldTickSampleBuffer>> Create(
            std::size_t maxSampleCount) noexcept;

        WorldTickSampleBuffer(const WorldTickSampleBuffer&) = delete;
        WorldTickSampleBuffer& operator=(const WorldTickSampleBuffer&) = delete;
        WorldTickSampleBuffer(WorldTickSampleBuffer&&) noexcept = default;
        WorldTickSampleBuffer& operator=(WorldTickSampleBuffer&&) noexcept = default;

        [[nodiscard]] std::size_t MaxSampleCount() const noexcept;
        [[nodiscard]] std::size_t SampleCount() const noexcept;
        [[nodiscard]] std::uint64_t DroppedSampleCount() const noexcept;
        [[nodiscard]] bool Empty() const noexcept;
        [[nodiscard]] bool Full() const noexcept;
        [[nodiscard]] bool TryRecord(const WorldTickSample& sample) noexcept;
        [[nodiscard]] std::span<const WorldTickSample> Samples() const noexcept;

    private:
        WorldTickSampleBuffer() noexcept = default;

        std::vector<WorldTickSample> storage_;
        std::size_t sampleCount_ = 0;
        std::uint64_t droppedSampleCount_ = 0;
    };
} // namespace psnr::world
