#pragma once

#include <mutex>

namespace psnr::core
{

    class NrMutex
    {
    public:
        NrMutex() = default;

        NrMutex(const NrMutex&) = delete;
        NrMutex& operator=(const NrMutex&) = delete;

        NrMutex(NrMutex&&) = delete;
        NrMutex& operator=(NrMutex&&) = delete;

        ~NrMutex() noexcept = default;

        void Lock()
        {
            mutex_.lock();
        }

        [[nodiscard]] bool TryLock()
        {
            return mutex_.try_lock();
        }

        void Unlock()
        {
            mutex_.unlock();
        }

    private:
        std::mutex mutex_;
    };

    template <typename TLock> class NrScopedLock
    {
    public:
        explicit NrScopedLock(TLock& lock)
            : lock_(&lock)
        {
            lock_->Lock();
        }

        NrScopedLock(const NrScopedLock&) = delete;
        NrScopedLock& operator=(const NrScopedLock&) = delete;

        NrScopedLock(NrScopedLock&&) = delete;
        NrScopedLock& operator=(NrScopedLock&&) = delete;

        ~NrScopedLock() noexcept
        {
            lock_->Unlock();
        }

    private:
        TLock* lock_;
    };

    class NrWaitLock final
    {
    public:
        explicit NrWaitLock(NrMutex& mutex)
            : mutex_(&mutex)
        {
            lock();
        }

        NrWaitLock(const NrWaitLock&) = delete;
        NrWaitLock& operator=(const NrWaitLock&) = delete;

        NrWaitLock(NrWaitLock&&) = delete;
        NrWaitLock& operator=(NrWaitLock&&) = delete;

        ~NrWaitLock() noexcept
        {
            if (locked_)
            {
                unlock();
            }
        }

        void lock()
        {
            mutex_->Lock();
            locked_ = true;
        }

        void unlock()
        {
            locked_ = false;
            mutex_->Unlock();
        }

    private:
        NrMutex* mutex_ = nullptr;
        bool locked_ = false;
    };

} // namespace psnr::core
