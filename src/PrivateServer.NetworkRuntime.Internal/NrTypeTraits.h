#pragma once

#include <concepts>
#include <type_traits>

namespace psnr::core
{

    template <typename T>
    concept NrConceptMoveOnly = std::is_move_constructible_v<T> && std::is_move_assignable_v<T> &&
                                !std::is_copy_constructible_v<T> && !std::is_copy_assignable_v<T>;

    template <typename T>
    concept NrConceptNoThrowDestructible = std::is_nothrow_destructible_v<T>;

    // The expression must be valid, and its result type must satisfy the type constraint(concept).
    template <typename T>
    concept NrConceptResettable = requires(T& value) {
        { value.Reset() } -> std::same_as<void>;
    };

    template <typename T>
    concept NrConceptValidityInspectable = requires(const T& value) {
        { value.IsValid() } -> std::same_as<bool>;
    };

    template <typename T>
    concept NrConceptMoveOnlyRaii = NrConceptMoveOnly<T> && NrConceptNoThrowDestructible<T> && NrConceptResettable<T> &&
                                    NrConceptValidityInspectable<T>;

    template <typename T>
    concept NrConceptObjectType = std::is_object_v<T>;

    template <typename T>
    concept NrConceptReferenceType = std::is_reference_v<T>;

    template <typename T>
    concept NrConceptTriviallyCopyable = std::is_trivially_copyable_v<T>;

    template <typename T, typename... Args> // typename... Args 는 타입 목록, Args... 는 받은 타입 목록을 다시 펼침
    concept NrConceptNoThrowConstructibleFrom = std::is_nothrow_constructible_v<T, Args...>;

    template <typename T>
    concept NrConceptNoThrowMoveConstructible = std::is_nothrow_move_constructible_v<T>;

    template <typename T>
    concept NrConceptNoThrowMoveAssignable = std::is_nothrow_move_assignable_v<T>;

    template <typename T>
    concept NrConceptUnsigned = std::is_unsigned_v<T>;

} // namespace psnr::core
