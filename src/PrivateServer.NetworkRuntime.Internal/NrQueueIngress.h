#pragma once

#include "NrIngress.h"
#include "NrInput.h"
#include "NrStatus.h"

namespace psnr::core
{
    template <typename T> class NrBoundedMpscQueue;

    class NrQueueIngress final : public NrIngress
    {
    public:
        explicit NrQueueIngress(NrBoundedMpscQueue<NrInput>& queue) noexcept;

        NrQueueIngress(const NrQueueIngress&) = delete;
        NrQueueIngress& operator=(const NrQueueIngress&) = delete;

        NrQueueIngress(NrQueueIngress&&) = delete;
        NrQueueIngress& operator=(NrQueueIngress&&) = delete;

        [[nodiscard]] NrStatus TryEnqueue(NrInput&& input) noexcept override;

    private:
        NrBoundedMpscQueue<NrInput>& queue_;
    };
} // namespace psnr::core
