#pragma once

#include "NrInput.h"
#include "NrPacketDispatchRule.h"
#include "NrPacketParser.h"
#include "NrResult.h"

namespace psnr::core
{
    class NrMemoryPoolManager;

    class NrInputFactory final
    {
    public:
        explicit NrInputFactory(NrMemoryPoolManager& memoryPoolManager) noexcept;

        NrInputFactory(const NrInputFactory&) = delete;
        NrInputFactory& operator=(const NrInputFactory&) = delete;

        [[nodiscard]] NrResult<NrInput> CreateInput(NrSessionKey sessionId,
                                                    const NrPacketParseResult& parseResult,
                                                    const NrPacketDispatchRule& dispatchRule) noexcept;

    private:
        NrMemoryPoolManager& memoryPoolManager_;
    };

} // namespace psnr::core
