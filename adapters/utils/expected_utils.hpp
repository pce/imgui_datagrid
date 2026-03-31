#pragma once
#include <expected>
#include <functional>
#include <optional>
#include <type_traits>
#include <utility>

namespace Adapters::Utils {

/// Returns expected<void,E>{} when cond is true, std::unexpected(msg) when false.
/// @tparam E Error type (deduced). Must be move-constructible.
template<typename E>
[[nodiscard]] inline std::expected<void, E> require(bool cond, E msg) noexcept(std::is_nothrow_move_constructible_v<E>)
{
    if (cond)
        return {};
    return std::unexpected(std::move(msg));
}

/// Wraps a nullable pointer into expected<reference_wrapper<T>,E>.
/// Returns std::ref(*ptr) on success, std::unexpected(msg) when null.
/// @tparam T Pointee type (deduced). @tparam E Error type (deduced).
template<typename T, typename E>
[[nodiscard]] inline std::expected<std::reference_wrapper<T>, E>
require_nonnull(T* ptr, E msg) noexcept(std::is_nothrow_move_constructible_v<E>)
{
    if (ptr)
        return std::ref(*ptr);
    return std::unexpected(std::move(msg));
}

/// Lifts std::optional<T> into expected<T,E>.
/// Returns the value when opt has a value, std::unexpected(msg) when empty.
/// @tparam T Value type (deduced). @tparam E Error type (deduced).
template<typename T, typename E>
[[nodiscard]] inline std::expected<T, E> value_or_error(std::optional<T> opt,
                                                        E msg) noexcept(std::is_nothrow_move_constructible_v<T> &&
                                                                        std::is_nothrow_move_constructible_v<E>)
{
    if (opt)
        return std::move(*opt);
    return std::unexpected(std::move(msg));
}

} // namespace Adapters::Utils
