#pragma once

#include "NrActor.h"
#include "NrActorScheduleGate.h"
#include "NrBoundedMpscQueue.h"

#include <cstddef>
#include <memory>
#include <new>
#include <utility>

namespace psnr::core
{
    template <typename TMessage> class NrActorMailbox
    {
        using NrMessageQueue = NrBoundedMpscQueue<TMessage>;

    public:
        NrActorMailbox(const NrActorMailbox&) = delete;
        NrActorMailbox& operator=(const NrActorMailbox&) = delete;

        NrActorMailbox(NrActorMailbox&&) = delete;
        NrActorMailbox& operator=(NrActorMailbox&&) = delete;

        ~NrActorMailbox() noexcept = default;

        [[nodiscard]] static NrResult<std::unique_ptr<NrActorMailbox>> Create(NrMemoryPoolManager& memoryPoolManager,
                                                                              NrMemoryPoolRole storageRole,
                                                                              std::size_t capacity) noexcept
        {
            NrResult<std::unique_ptr<NrMessageQueue>> queueResult =
                NrMessageQueue::Create(memoryPoolManager, storageRole, capacity);
            if (queueResult.Failed())
            {
                return NrResult<std::unique_ptr<NrActorMailbox>>::Failure(queueResult.Status());
            }

            std::unique_ptr<NrActorMailbox> mailbox(new (std::nothrow) NrActorMailbox(queueResult.TakeValue()));
            if (mailbox == nullptr)
            {
                return NrResult<std::unique_ptr<NrActorMailbox>>::Failure(NrErrorCode::OutOfMemory);
            }

            return NrResult<std::unique_ptr<NrActorMailbox>>(std::move(mailbox));
        }

        [[nodiscard]] std::size_t Capacity() const noexcept
        {
            return queue_->Capacity();
        }

        [[nodiscard]] std::size_t SizeApprox() const noexcept
        {
            return queue_->SizeApprox();
        }

        // forwarding reference
        template <typename U> [[nodiscard]] NrStatus TryEnqueue(U&& message) noexcept
        {
            return queue_->TryPush(std::forward<U>(message));
        }

        [[nodiscard]] NrStatus TryDequeue(TMessage& outMessage) noexcept
        {
            return queue_->TryPop(outMessage);
        }

    private:
        explicit NrActorMailbox(std::unique_ptr<NrMessageQueue> queue) noexcept
            : queue_(std::move(queue))
        {
        }

        std::unique_ptr<NrMessageQueue> queue_;
    };

    template <typename TMessage> class NrActorMailboxHandle
    {
    public:
        NrActorMailboxHandle() noexcept = default;

        NrActorMailboxHandle(NrActorMailbox<TMessage>& mailbox, NrActorScheduleGate& scheduleGate) noexcept
            : mailbox_(&mailbox)
            , scheduleGate_(&scheduleGate)
        {
        }

        [[nodiscard]] bool IsValid() const noexcept
        {
            return mailbox_ != nullptr && scheduleGate_ != nullptr;
        }

        [[nodiscard]] NrActorScheduleGate* ScheduleGate() const noexcept
        {
            return scheduleGate_;
        }

        template <typename U> [[nodiscard]] NrStatus TryEnqueue(U&& message) noexcept
        {
            if (!IsValid())
            {
                return NrStatus::Failure(NrErrorCode::InvalidState);
            }

            return mailbox_->TryEnqueue(std::forward<U>(message));
        }

    private:
        NrActorMailbox<TMessage>* mailbox_ = nullptr; // non-owning
        NrActorScheduleGate* scheduleGate_ = nullptr; // non-owning
    };
} // namespace psnr::core
