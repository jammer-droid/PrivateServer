#pragma once

namespace psnr::runtime
{
    enum class NrServerIoOpState
    {
        Initializing,
        Running,
        Draining,
        Stopping,
        Stopped,
    };

    enum class NrServerIoOpPollResult
    {
        Progressing,
        Ready,
        Completed,
    };

    class NrServerIoOpRules final
    {
    public:
        [[nodiscard]] static constexpr bool CanPostAccept(NrServerIoOpState state) noexcept
        {
            return state == NrServerIoOpState::Running;
        }

        [[nodiscard]] static constexpr bool CanRegisterAcceptedSession(NrServerIoOpState state) noexcept
        {
            return state == NrServerIoOpState::Running;
        }

        [[nodiscard]] static constexpr bool CanPostRecv(NrServerIoOpState state) noexcept
        {
            return state == NrServerIoOpState::Running;
        }

        [[nodiscard]] static constexpr bool CanPostNextRecvAfterCompletion(NrServerIoOpState state) noexcept
        {
            return state == NrServerIoOpState::Running;
        }

        [[nodiscard]] static constexpr bool CanPostSend(NrServerIoOpState state) noexcept
        {
            return state == NrServerIoOpState::Running;
        }

        [[nodiscard]] static constexpr bool CanProcessCompletionForCleanup(NrServerIoOpState state) noexcept
        {
            return state == NrServerIoOpState::Running || state == NrServerIoOpState::Draining ||
                   state == NrServerIoOpState::Stopping;
        }
    };

} // namespace psnr::runtime
