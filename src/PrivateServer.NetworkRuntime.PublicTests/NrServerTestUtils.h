#pragma once

#include <PrivateServer/NetworkRuntime/NrServer.h>

#include "gtest/gtest.h"

namespace psnr::runtime::tests
{
    using psnr::core::NrStatus;

    [[nodiscard]] inline NrServerConfig CreateServerConfig() noexcept
    {
        NrServerConfig config;
        config.bindEndpoint.port = 27015;
        return config;
    }

    inline void ExpectStatus(const NrStatus& actual, const NrStatus& expected)
    {
        EXPECT_EQ(actual.ErrorCode(), expected.ErrorCode());
        EXPECT_EQ(actual.NativeErrorCode(), expected.NativeErrorCode());
    }

    [[nodiscard]] inline NrServer CreateServer()
    {
        NrServerConfig config = CreateServerConfig();

        NrServer server;
        ExpectStatus(NrServer::Create(config, &server), NrStatus::Success());
        return server;
    }
} // namespace psnr::runtime::tests
