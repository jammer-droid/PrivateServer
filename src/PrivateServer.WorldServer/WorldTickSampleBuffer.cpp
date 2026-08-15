#include "pch.h"

#include "WorldTickSampleBuffer.h"

#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace psnr::world
{
    WorldResult<std::unique_ptr<WorldTickSampleBuffer>> WorldTickSampleBuffer::Create(
        const std::size_t maxSampleCount) noexcept
    {
        if (maxSampleCount == 0)
        {
            return WorldResult<std::unique_ptr<WorldTickSampleBuffer>>::Failure(WorldErrorCode::InvalidCapacity);
        }

        std::unique_ptr<WorldTickSampleBuffer> buffer{new (std::nothrow) WorldTickSampleBuffer{}};
        if (buffer == nullptr)
        {
            return WorldResult<std::unique_ptr<WorldTickSampleBuffer>>::Failure(WorldErrorCode::AllocationFailed);
        }
        try
        {
            buffer->storage_.resize(maxSampleCount);
            return WorldResult<std::unique_ptr<WorldTickSampleBuffer>>{std::move(buffer)};
        }
        catch (const std::bad_alloc&)
        {
            return WorldResult<std::unique_ptr<WorldTickSampleBuffer>>::Failure(WorldErrorCode::AllocationFailed);
        }
        catch (const std::length_error&)
        {
            return WorldResult<std::unique_ptr<WorldTickSampleBuffer>>::Failure(WorldErrorCode::InvalidCapacity);
        }
    }

    std::size_t WorldTickSampleBuffer::MaxSampleCount() const noexcept
    {
        return storage_.size();
    }

    std::size_t WorldTickSampleBuffer::SampleCount() const noexcept
    {
        return sampleCount_;
    }

    std::uint64_t WorldTickSampleBuffer::DroppedSampleCount() const noexcept
    {
        return droppedSampleCount_;
    }

    bool WorldTickSampleBuffer::Empty() const noexcept
    {
        return sampleCount_ == 0;
    }

    bool WorldTickSampleBuffer::Full() const noexcept
    {
        return sampleCount_ == storage_.size();
    }

    bool WorldTickSampleBuffer::TryRecord(const WorldTickSample& sample) noexcept
    {
        if (Full())
        {
            if (droppedSampleCount_ != std::numeric_limits<std::uint64_t>::max())
            {
                ++droppedSampleCount_;
            }
            return false;
        }

        storage_[sampleCount_] = sample;
        ++sampleCount_;
        return true;
    }

    std::span<const WorldTickSample> WorldTickSampleBuffer::Samples() const noexcept
    {
        return std::span<const WorldTickSample>{storage_.data(), sampleCount_};
    }
} // namespace psnr::world
