#include "pch.h"

#include "WorldBodyTrailComponent.h"

#include <new>
#include <stdexcept>
#include <utility>

namespace psnr::world
{
    WorldResult<BodyTrailComponent> CreateBodyTrailComponent(const std::uint32_t maxSampleCount) noexcept
    {
        if (maxSampleCount == 0)
        {
            return WorldResult<BodyTrailComponent>::Failure(WorldErrorCode::InvalidCapacity);
        }

        try
        {
            BodyTrailComponent created;
            created.storage_.resize(maxSampleCount);
            return WorldResult<BodyTrailComponent>{std::move(created)};
        }
        catch (const std::bad_alloc&)
        {
            return WorldResult<BodyTrailComponent>::Failure(WorldErrorCode::AllocationFailed);
        }
        catch (const std::length_error&)
        {
            return WorldResult<BodyTrailComponent>::Failure(WorldErrorCode::InvalidCapacity);
        }
    }

    bool BodyTrailComponent::IsInitialized() const noexcept
    {
        return !storage_.empty();
    }

    std::size_t BodyTrailComponent::MaxSampleCount() const noexcept
    {
        return storage_.size();
    }

    std::size_t BodyTrailComponent::SampleCount() const noexcept
    {
        return sampleCount_;
    }

    bool BodyTrailComponent::Empty() const noexcept
    {
        return sampleCount_ == 0;
    }

    bool BodyTrailComponent::TryPushFront(const BodyTrailSample sample) noexcept
    {
        if (!IsInitialized() || sampleCount_ >= storage_.size())
        {
            return false;
        }

        frontIndex_ = sampleCount_ == 0 ? 0 : (frontIndex_ + storage_.size() - 1) % storage_.size();
        storage_[frontIndex_] = sample;
        ++sampleCount_;
        return true;
    }

    bool BodyTrailComponent::TryPushBack(const BodyTrailSample sample) noexcept
    {
        if (!IsInitialized() || sampleCount_ >= storage_.size())
        {
            return false;
        }

        storage_[PhysicalIndex(sampleCount_)] = sample;
        ++sampleCount_;
        return true;
    }

    bool BodyTrailComponent::TryRead(const std::size_t logicalIndex, BodyTrailSample* const outSample) const noexcept
    {
        if (outSample == nullptr || logicalIndex >= sampleCount_)
        {
            return false;
        }

        *outSample = storage_[PhysicalIndex(logicalIndex)];
        return true;
    }

    bool BodyTrailComponent::TryWrite(const std::size_t logicalIndex, const BodyTrailSample sample) noexcept
    {
        if (logicalIndex >= sampleCount_)
        {
            return false;
        }

        storage_[PhysicalIndex(logicalIndex)] = sample;
        return true;
    }

    bool BodyTrailComponent::TryTrimBack(const std::size_t retainedSampleCount) noexcept
    {
        if (retainedSampleCount > sampleCount_)
        {
            return false;
        }

        sampleCount_ = retainedSampleCount;
        if (sampleCount_ == 0)
        {
            frontIndex_ = 0;
        }
        return true;
    }

    bool operator==(const BodyTrailComponent& left, const BodyTrailComponent& right) noexcept
    {
        if (left.MaxSampleCount() != right.MaxSampleCount() || left.SampleCount() != right.SampleCount())
        {
            return false;
        }

        for (std::size_t index = 0; index < left.SampleCount(); ++index)
        {
            BodyTrailSample leftSample;
            BodyTrailSample rightSample;
            if (!left.TryRead(index, &leftSample) || !right.TryRead(index, &rightSample) || leftSample != rightSample)
            {
                return false;
            }
        }
        return true;
    }

    std::size_t BodyTrailComponent::PhysicalIndex(const std::size_t logicalIndex) const noexcept
    {
        return (frontIndex_ + logicalIndex) % storage_.size();
    }
} // namespace psnr::world
