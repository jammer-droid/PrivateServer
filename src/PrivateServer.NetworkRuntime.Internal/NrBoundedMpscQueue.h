#pragma once

#include "NrMemoryMath.h"
#include "NrMemoryPoolManager.h"
#include "NrTypeTraits.h"
#include "NrResult.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace psnr::core
{
    /*
      Queue storage role block
      ┌──────────────────────────────────────┐
      │ Slot[0]                              │
      │   sequence                           │
      │   storage bytes for T                │
      ├──────────────────────────────────────┤
      │ Slot[1]                              │
      │   sequence                           │
      │   storage bytes for T                │
      ├──────────────────────────────────────┤
      │ Slot[2]                              │
      │   sequence                           │
      │   storage bytes for T                │
      └──────────────────────────────────────┘
    */

    inline constexpr std::size_t NrCacheLineSize = 64;

    template <typename T> struct alignas(NrCacheLineSize) NrBoundedMpscQueueSlot
    {
        std::atomic<std::size_t> sequence;
        alignas(T) std::byte storage[sizeof(T)]; //
    };

    template <typename T> class NrBoundedMpscQueue
    {
        static_assert(NrConceptObjectType<T>, "NrBoundedMpscQueue<T> requires an object type.");
        static_assert(NrConceptNoThrowDestructible<T>, "NrBoundedMpscQueue<T> requires a noexcept destructor.");

    public:
        NrBoundedMpscQueue(const NrBoundedMpscQueue&) = delete;
        NrBoundedMpscQueue& operator=(const NrBoundedMpscQueue&) = delete;

        NrBoundedMpscQueue(NrBoundedMpscQueue&&) = delete;
        NrBoundedMpscQueue& operator=(NrBoundedMpscQueue&&) = delete;

        ~NrBoundedMpscQueue() noexcept
        {
            DestroySlots();
        }

        [[nodiscard]] static NrResult<std::unique_ptr<NrBoundedMpscQueue>> Create(
            NrMemoryPoolManager& memoryPoolManager, NrMemoryPoolRole storageRole, std::size_t capacity) noexcept
        {
            NrResult<std::size_t> requiredBytesResult = RequiredStorageBytes(capacity);
            if (requiredBytesResult.Failed())
            {
                return NrResult<std::unique_ptr<NrBoundedMpscQueue>>::Failure(requiredBytesResult.Status());
            }

            const std::size_t requiredBytes = requiredBytesResult.Value();
            NrResult<NrPooledMemoryBlock> blockResult = memoryPoolManager.AcquireBlock(storageRole);
            if (blockResult.Failed())
            {
                return NrResult<std::unique_ptr<NrBoundedMpscQueue>>::Failure(blockResult.Status());
            }

            NrPooledMemoryBlock block = blockResult.TakeValue();
            if (!block.IsValid() || block.Data() == nullptr)
            {
                return NrResult<std::unique_ptr<NrBoundedMpscQueue>>::Failure(NrErrorCode::InvalidState);
            }

            if (block.Capacity() < requiredBytes)
            {
                return NrResult<std::unique_ptr<NrBoundedMpscQueue>>::Failure(NrErrorCode::CapacityExceeded);
            }

            if (!utils::NrIsAligned(block.Data(), alignof(NrSlot)))
            {
                return NrResult<std::unique_ptr<NrBoundedMpscQueue>>::Failure(NrErrorCode::InvalidState);
            }

            std::unique_ptr<NrBoundedMpscQueue> queue(new (std::nothrow)
                                                          NrBoundedMpscQueue(std::move(block), capacity));
            if (queue == nullptr)
            {
                return NrResult<std::unique_ptr<NrBoundedMpscQueue>>::Failure(NrErrorCode::OutOfMemory);
            }

            queue->InitializeSlots();
            return NrResult<std::unique_ptr<NrBoundedMpscQueue>>(std::move(queue));
        }

        [[nodiscard]] static NrResult<std::size_t> RequiredStorageBytes(std::size_t capacity) noexcept
        {
            if (capacity < 2 || !utils::NrIsPowerOfTwo(capacity))
            {
                return NrResult<std::size_t>::Failure(NrErrorCode::InvalidArgument);
            }

            std::size_t requiredBytes = 0;
            if (!utils::NrTryMultiply(sizeof(NrSlot), capacity, requiredBytes))
            {
                return NrResult<std::size_t>::Failure(NrErrorCode::CapacityExceeded);
            }

            return NrResult<std::size_t>(requiredBytes);
        }

        [[nodiscard]] std::size_t Capacity() const noexcept
        {
            return capacity_;
        }

        [[nodiscard]] std::size_t SizeApprox() const noexcept
        {
            const std::size_t enqueuePos = enqueuePos_.load(std::memory_order_relaxed);
            const std::size_t dequeuePos = dequeuePos_.load(std::memory_order_relaxed);
            return enqueuePos >= dequeuePos ? enqueuePos - dequeuePos : 0;
        }

        // forwarding-reference(함수 템플릿 + 인자가 T&& 형식)
        // value의 타입이 lvalue인 경우 U = Type& 로 해석 -> 파라미터에 대입 U&& = Type& && -> collapsing 되어 Type&
        // value의 타입이 rvalue인 경우 U = Type 로 해석 -> 파라미터에 대입 U&& = Type&&
        template <typename U> [[nodiscard]] NrStatus TryPush(U&& value) noexcept
        {
            static_assert(NrConceptNoThrowConstructibleFrom<T, U>,
                          "NrBoundedMpscQueue<T>::TryPush requires noexcept construction from U.");

            std::size_t enqueuePos = enqueuePos_.load(std::memory_order_relaxed);

            while (true)
            {
                NrSlot& slot = SlotAt(enqueuePos & capacityMask_);

                // 1. slot 사용 여부 확인
                //      - TryPop에서 해당 slot.seq를 release 했는지 확인
                const std::size_t sequence = slot.sequence.load(std::memory_order_acquire);
                const std::intptr_t sequenceDiff =
                    static_cast<std::intptr_t>(sequence) - static_cast<std::intptr_t>(enqueuePos);

                if (sequenceDiff == 0)
                {
                    // 2. slot을 실제로 예약/사용
                    //      - slot 점유용 CAS(스레드 경합 가능, 성공한 스레드만 slot item을 생성)
                    //      - 성공하면 atomic 변수 값을 desired로 변경, expected는 그대로 유지
                    //      - 실패하면 atomic 변수 값은 그대로, expected를 atomic의 실제 값으로 갱신
                    if (enqueuePos_.compare_exchange_weak(enqueuePos, enqueuePos + 1, std::memory_order_relaxed,
                                                          std::memory_order_relaxed))
                    {
                        // U가 lvalue이면 copy constructor 호출, rvalue이면 move constructor 호출
                        std::construct_at(ItemPointer(slot), std::forward<U>(value));
                        slot.sequence.store(enqueuePos + 1, std::memory_order_release);
                        return NrStatus();
                    }

                    continue; // CAS 경합 실패, 재시도
                }

                if (sequenceDiff < 0)
                {
                    return NrStatus(NrErrorCode::QueueFull); // Queue 공간 없음
                }

                enqueuePos = enqueuePos_.load(
                    std::memory_order_relaxed); // 현재 스레드의 enqueuePos 상태가 앞서 있는 상태. 재시도
            }
        }

        [[nodiscard]] NrStatus TryPop(T& out) noexcept
        {
            static_assert(NrConceptNoThrowMoveAssignable<T>,
                          "NrBoundedMpscQueue<T>::TryPop requires noexcept move assignment.");

            const std::size_t dequeuePos = dequeuePos_.load(std::memory_order_relaxed);
            NrSlot& slot = SlotAt(dequeuePos & capacityMask_);

            // 1. slot 채워져 있는지 확인
            const std::size_t sequence = slot.sequence.load(std::memory_order_acquire);
            if (sequence != dequeuePos + 1)
            {
                return NrStatus(NrErrorCode::QueueEmpty);
            }

            // 2. pop slot item
            T* item = ItemPointer(slot);
            out = std::move(*item);
            std::destroy_at(item);

            slot.sequence.store(dequeuePos + capacity_, std::memory_order_release); // 사용 완료 후 해당 slot release
            dequeuePos_.store(dequeuePos + 1, std::memory_order_relaxed);
            return NrStatus();
        }

    private:
        using NrSlot = NrBoundedMpscQueueSlot<T>;

        NrBoundedMpscQueue(NrPooledMemoryBlock storageBlock, std::size_t capacity) noexcept
            : storageBlock_(std::move(storageBlock))
            , capacity_(capacity)
            , capacityMask_(capacity - 1)
        {
        }

        void InitializeSlots() noexcept
        {
            for (std::size_t index = 0; index < capacity_; ++index)
            {
                NrSlot* slot = SlotPointer(index);
                std::construct_at(slot);
                slot->sequence.store(index, std::memory_order_relaxed);
            }
        }

        void DestroySlots() noexcept
        {
            DestroyRemainingItems();

            for (std::size_t index = 0; index < capacity_; ++index)
            {
                std::destroy_at(SlotPointer(index));
            }
        }

        void DestroyRemainingItems() noexcept
        {
            std::size_t dequeuePos = dequeuePos_.load(std::memory_order_relaxed);
            const std::size_t enqueuePos = enqueuePos_.load(std::memory_order_relaxed);

            while (dequeuePos < enqueuePos)
            {
                NrSlot& slot = SlotAt(dequeuePos & capacityMask_);
                if (slot.sequence.load(std::memory_order_acquire) == dequeuePos + 1)
                {
                    std::destroy_at(ItemPointer(slot));
                    slot.sequence.store(dequeuePos + capacity_, std::memory_order_release);
                }

                ++dequeuePos;
            }

            dequeuePos_.store(enqueuePos, std::memory_order_relaxed);
        }

        // index = enqueuePos % mask와 동일(capacity가 power-of-two인 경우)
        [[nodiscard]] NrSlot& SlotAt(std::size_t index) noexcept
        {
            return *SlotPointer(index);
        }

        [[nodiscard]] NrSlot* SlotPointer(std::size_t index) noexcept
        {
            return reinterpret_cast<NrSlot*>(storageBlock_.Data()) + index;
        }

        [[nodiscard]] T* ItemPointer(NrSlot& slot) noexcept
        {
            // launder: storage 주소에 들어오는 객체를 T* 로 해석할 때, 새로운 T* 로 해석
            // - storage 는 raw byte 배열이기 때문에 새로운 객체를 만들 때
            // - 컴파일러가 이전에 해석된 포인터를 믿지 않고, 새롭게 만든 T*를 얻을 수 있도록
            return std::launder(reinterpret_cast<T*>(slot.storage));
        }

        NrPooledMemoryBlock storageBlock_;
        std::size_t capacity_ = 0;     // queue가 가진 slot 개수, power-of-two
        std::size_t capacityMask_ = 0; // ring buffer index 계산용 마스크

        // producer가 지금까지 push 한 위치
        alignas(NrCacheLineSize) std::atomic<std::size_t> enqueuePos_{0};
        // consumer가 지금까지 pop 한 위치
        alignas(NrCacheLineSize) std::atomic<std::size_t> dequeuePos_{0};
        // queue size = enqueuePos - dequeuePos
    };

} // namespace psnr::core
