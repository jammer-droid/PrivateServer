#pragma once

#include "NrToWorldEvent.h"

#include <memory>

namespace psnr::runtime::internal
{
    class NrToWorldEventAccess final
    {
    public:
        [[nodiscard]] static NrStatus Adopt(std::unique_ptr<NrToWorldHandoffEvent> event,
                                            NrToWorldEvent& outEvent) noexcept;
    };
} // namespace psnr::runtime::internal
