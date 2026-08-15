#pragma once

#include "NrStatus.h"
#include "NrTypeTraits.h"

#include <cassert>
#include <optional>
#include <utility>

namespace psnr::core
{

    template <typename T> class NrResult
    {
        static_assert(!NrConceptReferenceType<T>, "NrResult<T> does not support reference values.");

    public:
        explicit NrResult(T value)
            : status_()
            , value_(std::move(value))
        {
        }

        [[nodiscard]] static NrResult Failure(NrStatus status) noexcept
        {
            return NrResult(status);
        }

        [[nodiscard]] static NrResult Failure(NrErrorCode errorCode, NrNativeErrorCode nativeErrorCode = 0) noexcept
        {
            return NrResult(NrStatus(errorCode, nativeErrorCode));
        }

        [[nodiscard]] const NrStatus& Status() const noexcept
        {
            return status_;
        }

        [[nodiscard]] NrErrorCode ErrorCode() const noexcept
        {
            return status_.ErrorCode();
        }

        [[nodiscard]] NrNativeErrorCode NativeErrorCode() const noexcept
        {
            return status_.NativeErrorCode();
        }

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return status_.Succeeded();
        }

        [[nodiscard]] bool Failed() const noexcept
        {
            return status_.Failed();
        }

        [[nodiscard]] bool HasValue() const noexcept
        {
            return value_.has_value();
        }

        [[nodiscard]] T& Value() noexcept
        {
            assert(Succeeded());
            assert(value_.has_value());
            return *value_;
        }

        [[nodiscard]] const T& Value() const noexcept
        {
            assert(Succeeded());
            assert(value_.has_value());
            return *value_;
        }

        // noexcept follows whether T can be moved without throwing.
        // 1. *value -> lvalue
        // 2. std::move(*value) -> lvalue를 T&&로 캐스팅(아직 이동 X)
        // 3. 반환 타입 T로 반환 -> 반환값 객체 생성을 위해 T의 move constructor가 호출
        [[nodiscard]] T TakeValue() noexcept(std::is_nothrow_move_constructible_v<T>)
        {
            assert(Succeeded());
            assert(value_.has_value());
            return std::move(*value_);
        }

    private:
        explicit NrResult(NrStatus status) noexcept
            : status_(status)
        {
            assert(status_.Failed());
        }

        NrStatus status_;
        std::optional<T> value_;
    };

} // namespace psnr::core
