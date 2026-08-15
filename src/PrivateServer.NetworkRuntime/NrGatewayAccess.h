#pragma once

#include "NrGateway.h"
#include "NrServerSubmissionGate.h"

namespace psnr::core
{
    class NrMemoryPoolManager;
}

namespace psnr::runtime::internal
{
    class NrGatewayAccess final
    {
    public:
        [[nodiscard]] static psnr::core::NrStatus Create(NrSubmissionAdmissionHandle submissionAdmission,
                                                         psnr::core::NrMemoryPoolManager& memoryPoolManager,
                                                         NrGateway& outGateway) noexcept;
    };
} // namespace psnr::runtime::internal
