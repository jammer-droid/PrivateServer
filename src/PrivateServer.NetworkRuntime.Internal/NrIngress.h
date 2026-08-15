#pragma once

#include "NrInput.h"
#include "NrStatus.h"

namespace psnr::core
{
    class NrIngress
    {
    public:
        virtual ~NrIngress() noexcept = default;

        [[nodiscard]] virtual NrStatus TryEnqueue(NrInput&& input) noexcept = 0;
    };

} // namespace psnr::core
