#pragma once

#include <cassert>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <variant>

namespace psnr::world
{
    enum class WorldErrorCode : std::uint8_t
    {
        InvalidArgument = 0,
        InvalidCapacity,
        InvalidInput,
        InvalidState,
        InvalidConfig,
        MalformedPayload,
        NotFound,
        AlreadyExists,
        InvalidSessionSet,
        NonSequentialTick,
        ArithmeticOverflow,
        InitialPenetration,
        AllocationFailed,
        CapacityExceeded,
        DependencyFailure,
        OperationFailed,
    };

    // TError 기본 타입 = WorldErrorCode
    template <typename TValue, typename TError = WorldErrorCode> class WorldResult final
    {
        static_assert(!std::is_reference_v<TValue>, "WorldResult does not support reference values.");
        static_assert(!std::is_reference_v<TError>, "WorldResult does not support reference errors.");

    public:
        explicit WorldResult(TValue value) noexcept(std::is_nothrow_move_constructible_v<TValue>)
            : storage_(std::in_place_index<0>, std::move(value))
        {
        }

        WorldResult(const WorldResult&) = default;
        WorldResult(WorldResult&&) = default;
        WorldResult& operator=(const WorldResult&) = delete;
        WorldResult& operator=(WorldResult&&) = delete;

        [[nodiscard]] static WorldResult Failure(TError error) noexcept(std::is_nothrow_move_constructible_v<TError>)
        {
            return WorldResult(FailureTag{}, std::move(error));
        }

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return storage_.index() == 0; // 현재 활성화된 타입이 variant 목록 중 몇 번째인지
        }

        [[nodiscard]] bool Failed() const noexcept
        {
            return storage_.index() == 1; // 현재 활성화된 타입이 variant 목록 중 몇 번째인지
        }

        [[nodiscard]] bool HasValue() const noexcept
        {
            return Succeeded();
        }

        [[nodiscard]] TValue& Value() noexcept
        {
            assert(Succeeded());
            return std::get<0>(storage_);
        }

        [[nodiscard]] const TValue& Value() const noexcept
        {
            assert(Succeeded());
            return std::get<0>(storage_);
        }

        [[nodiscard]] TValue TakeValue() noexcept(std::is_nothrow_move_constructible_v<TValue>)
        {
            assert(Succeeded());
            return std::move(std::get<0>(storage_));
        }

        [[nodiscard]] TError& Error() noexcept
        {
            assert(Failed());
            return std::get<1>(storage_);
        }

        [[nodiscard]] const TError& Error() const noexcept
        {
            assert(Failed());
            return std::get<1>(storage_);
        }

    private:
        struct FailureTag final
        {
        };

        WorldResult(FailureTag, TError error) noexcept(std::is_nothrow_move_constructible_v<TError>)
            : storage_(std::in_place_index<1>, std::move(error))
        {
        }

        std::variant<TValue, TError> storage_; // 성공 or 실패만 저장
    };

    // 반환할 성공 값이 없는 경우 사용, TError 기본값은 템플릿 원본 WorldErrorCode 받음
    template <typename TError> class WorldResult<void, TError> final
    {
        static_assert(!std::is_reference_v<TError>, "WorldResult does not support reference errors.");

    public:
        [[nodiscard]] static WorldResult Success() noexcept
        {
            return WorldResult(SuccessTag{});
        }

        [[nodiscard]] static WorldResult Failure(TError error) noexcept(std::is_nothrow_move_constructible_v<TError>)
        {
            return WorldResult(FailureTag{}, std::move(error));
        }

        WorldResult(const WorldResult&) = default;
        WorldResult(WorldResult&&) = default;
        WorldResult& operator=(const WorldResult&) = delete;
        WorldResult& operator=(WorldResult&&) = delete;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return storage_.index() == 0;
        }

        [[nodiscard]] bool Failed() const noexcept
        {
            return storage_.index() == 1;
        }

        [[nodiscard]] TError& Error() noexcept
        {
            assert(Failed());
            return std::get<1>(storage_);
        }

        [[nodiscard]] const TError& Error() const noexcept
        {
            assert(Failed());
            return std::get<1>(storage_);
        }

    private:
        struct SuccessTag final
        {
        };

        struct FailureTag final
        {
        };

        explicit WorldResult(SuccessTag) noexcept
            : storage_(std::in_place_index<0>)
        {
        }

        WorldResult(FailureTag, TError error) noexcept(std::is_nothrow_move_constructible_v<TError>)
            : storage_(std::in_place_index<1>, std::move(error))
        {
        }

        std::variant<std::monostate, TError> storage_; // 성공 or 실패만 저장, 빈 타입인 std::monostate (별도 값 없음)
    };
} // namespace psnr::world
