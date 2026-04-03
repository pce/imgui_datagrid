#pragma once
/// @file compat/move_only_function.hpp
///
/// Provides compat::move_only_function<Sig> — a portable, move-only type-erased
/// callable, equivalent to C++23 std::move_only_function.
///
/// Selection order:
///   1. std::move_only_function  — when __cpp_lib_move_only_function is defined
///                                 (upstream LLVM libc++ once it ships the feature)
///   2. hana23::move_only_function — from hanickadot/polyfill-23 (FetchContent dep)
///
/// Usage:
///   #include "compat/move_only_function.hpp"
///   using MyFn = compat::move_only_function<void(int) noexcept>;

#include <version>

#ifdef __cpp_lib_move_only_function
// ── Path 1: stdlib has it ────────────────────────────────────────────────────
#    include <functional>
namespace compat {
template <typename Sig>
using move_only_function = std::move_only_function<Sig>;
} // namespace compat

#elif defined(COMPAT_HAS_HANA_POLYFILL23)
// ── Path 2: hana23 polyfill (FetchContent hana_polyfill23) ───────────────────
#    include <hana23/move_only_function.hpp>
namespace compat {
template <typename Sig>
using move_only_function = hana23::move_only_function<Sig>;
} // namespace compat

#else
// ── Path 3: self-contained fallback (unique_ptr type erasure) ────────────────
//
// Limitations vs std::move_only_function:
//   • Every callable is heap-allocated (no small-buffer optimisation).
//   • The noexcept qualifier on the signature is accepted syntactically but is
//     not enforced at the type level (the call operator is not marked noexcept).
//     Add -DCOMPAT_HAS_HANA_POLYFILL23 + the FetchContent dep for full fidelity.
#    include <memory>
#    include <type_traits>
#    include <utility>

namespace compat {

template <typename Sig>
class move_only_function; // primary template — intentionally incomplete

template <typename R, typename... Args>
class move_only_function<R(Args...)> {
    struct Base {
        virtual ~Base()                    = default;
        virtual R call(Args&&... a)        = 0;
    };
    template <typename F>
    struct Impl final : Base {
        F f;
        explicit Impl(F&& fn) : f(std::move(fn)) {}
        R call(Args&&... a) override { return f(std::forward<Args>(a)...); }
    };

    std::unique_ptr<Base> impl_;

public:
    move_only_function() noexcept = default;

    template <typename F,
              typename = std::enable_if_t<!std::is_same_v<std::decay_t<F>, move_only_function>>>
    // NOLINTNEXTLINE(bugprone-forwarding-reference-overload)
    explicit(false) move_only_function(F&& f)
        : impl_(std::make_unique<Impl<std::decay_t<F>>>(std::forward<F>(f))) {}

    move_only_function(move_only_function&&) noexcept            = default;
    move_only_function& operator=(move_only_function&&) noexcept = default;

    move_only_function(const move_only_function&)            = delete;
    move_only_function& operator=(const move_only_function&) = delete;

    explicit operator bool() const noexcept { return impl_ != nullptr; }

    R operator()(Args... a) { return impl_->call(std::forward<Args>(a)...); }
    R operator()(Args... a) const
    {
        return const_cast<move_only_function*>(this)->impl_->call(std::forward<Args>(a)...);
    }
};

// Partial specialisation: strip the noexcept qualifier — the fallback cannot
// enforce it at the type level, but the template is otherwise identical.
template <typename R, typename... Args>
class move_only_function<R(Args...) noexcept> : public move_only_function<R(Args...)> {
    using Base = move_only_function<R(Args...)>;
public:
    using Base::Base;
    using Base::operator=;
    using Base::operator bool;
    using Base::operator();
};

} // namespace compat
#endif // fallback

