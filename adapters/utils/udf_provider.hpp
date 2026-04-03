#pragma once
#include "../data_source.hpp"

#include <any>
#include <concepts>
#include <expected>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace datagrid::adapters {

/// Adapter-agnostic representation of a scalar value type used in UDF
/// signatures.  Intentionally minimal; extend as needed.
enum class ScalarType : uint8_t
{
    Bool,
    Int32,
    Int64,
    Float32,
    Float64,
    Varchar,
};

[[nodiscard]] constexpr std::string_view name_of(ScalarType t) noexcept
{
    switch (t) {
        case ScalarType::Bool:
            return "bool";
        case ScalarType::Int32:
            return "int32";
        case ScalarType::Int64:
            return "int64";
        case ScalarType::Float32:
            return "float32";
        case ScalarType::Float64:
            return "float64";
        case ScalarType::Varchar:
            return "varchar";
    }
    return "unknown";
}

/// Primary template — intentionally left undefined; a missing specialisation
/// causes a clear compile error.
template<typename T>
struct ScalarTypeTraits;

template<>
struct ScalarTypeTraits<bool>
{
    static constexpr ScalarType value = ScalarType::Bool;
};
template<>
struct ScalarTypeTraits<int32_t>
{
    static constexpr ScalarType value = ScalarType::Int32;
};
template<>
struct ScalarTypeTraits<int64_t>
{
    static constexpr ScalarType value = ScalarType::Int64;
};
// Note: int / long long are intentionally omitted — on Apple Clang (and most
// 64-bit POSIX toolchains) int32_t IS int and int64_t IS long long, so adding
// explicit specialisations for those plain types would cause a "redefinition"
// error.  Use int32_t / int64_t at call sites for unambiguous mapping.
template<>
struct ScalarTypeTraits<float>
{
    static constexpr ScalarType value = ScalarType::Float32;
};
template<>
struct ScalarTypeTraits<double>
{
    static constexpr ScalarType value = ScalarType::Float64;
};
template<>
struct ScalarTypeTraits<std::string>
{
    static constexpr ScalarType value = ScalarType::Varchar;
};
template<>
struct ScalarTypeTraits<std::string_view>
{
    static constexpr ScalarType value = ScalarType::Varchar;
};

/// Convenience variable template — strips cv-ref before the lookup so that
/// ScalarTypeOf<const int&> works exactly like ScalarTypeOf<int>.
template<typename T>
inline constexpr ScalarType ScalarTypeOf = ScalarTypeTraits<std::remove_cvref_t<T>>::value;

/// ScalarCallable<F, R, Args...>
/// Satisfied when F is invocable with Args... and its result converts to R.
/// This is used both to constrain RegisterScalar and as documentation of the
/// expected callable shape.
template<typename F, typename R, typename... Args>
concept ScalarCallable = std::invocable<F, Args...> && std::convertible_to<std::invoke_result_t<F, Args...>, R>;

/// Carries every piece of information an adapter needs to register one scalar
/// UDF.  Created in the type-safe RegisterScalar template before crossing the
/// virtual call boundary.
///
/// `callable` holds a std::function<R(Args...)>.  To reconstruct it on the
/// adapter side, use VisitScalarType() to recover R and each Arg, then call:
///
///   auto* fn = std::any_cast<std::function<R(Args...)>>(&desc.callable);
///
struct ScalarUDFDesc
{
    std::string             name;
    std::vector<ScalarType> argTypes;
    ScalarType              returnType = ScalarType::Int32;
    std::any                callable; ///< std::function<R(Args...)>
};

/// Call fn with a std::type_identity<T> tag corresponding to the runtime
/// ScalarType t.  The generic-lambda form (C++20/23) lets callers write:
///
///   VisitScalarType(t, [&]<typename T>(std::type_identity<T>) {
///       // T is now a concrete type matching t at runtime
///   });
///
/// All six ScalarType values are covered; std::unreachable() guards the
/// dead default branch so compilers can see the switch is exhaustive.
template<typename Fn>
[[nodiscard]] auto VisitScalarType(ScalarType t, Fn&& fn) -> decltype(fn(std::type_identity<bool>{}))
{
    switch (t) {
        case ScalarType::Bool:
            return fn(std::type_identity<bool>{});
        case ScalarType::Int32:
            return fn(std::type_identity<int32_t>{});
        case ScalarType::Int64:
            return fn(std::type_identity<int64_t>{});
        case ScalarType::Float32:
            return fn(std::type_identity<float>{});
        case ScalarType::Float64:
            return fn(std::type_identity<double>{});
        case ScalarType::Varchar:
            return fn(std::type_identity<std::string>{});
    }
    std::unreachable();
}

class IUDFProvider
{
  public:
    virtual ~IUDFProvider() = default;

    IUDFProvider(const IUDFProvider&)            = delete;
    IUDFProvider& operator=(const IUDFProvider&) = delete;
    IUDFProvider(IUDFProvider&&)                 = default;
    IUDFProvider& operator=(IUDFProvider&&)      = default;

    /// Returns true when this adapter can register UDFs.
    /// Always check this before calling Register*.
    [[nodiscard]] virtual bool SupportsUDF() const noexcept { return false; }

    /// Register a simple scalar UDF.
    ///
    /// Template parameters:
    ///   R      — return type   (e.g. int32_t, double, std::string)
    ///   Args   — argument type(s)
    ///   F      — callable type (deduced); must satisfy ScalarCallable<F,R,Args...>
    ///
    /// The adapter internally vectorises the call — for DuckDB this means
    /// building a DataChunk / UnifiedVectorFormat trampoline.  The caller
    /// never deals with DataChunk.
    ///
    /// Returns std::unexpected on failure (not connected, type not supported,
    /// name already registered, …).
    template<typename R, typename... Args, typename F>
        requires ScalarCallable<F, R, Args...>
    [[nodiscard]] std::expected<void, Error> RegisterScalar(std::string_view name, F&& fn)
    {
        return RegisterScalarImpl(ScalarUDFDesc{
            .name       = std::string(name),
            .argTypes   = {ScalarTypeOf<Args>...},
            .returnType = ScalarTypeOf<R>,
            .callable   = std::function<R(Args...)>{std::forward<F>(fn)},
        });
    }

    /// Register multiple UDFs in one call.  Stops and returns the first
    /// error encountered; successfully registered functions are NOT rolled
    /// back (adapters rarely support transactional DDL).
    [[nodiscard]] std::expected<void, Error> RegisterAll(std::initializer_list<ScalarUDFDesc> descs)
    {
        for (auto desc : descs) {
            if (auto res = RegisterScalarImpl(std::move(desc)); !res)
                return res;
        }
        return {};
    }

  protected:
    IUDFProvider() = default;

    /// Override in concrete adapters.
    ///
    /// The default returns an error — base class is intentionally a no-op so
    /// that adapters that don't yet support UDFs compile cleanly.
    ///
    /// Adapters should:
    ///   1. Check IsConnected() (or equivalent).
    ///   2. Use VisitScalarType() to decode desc.argTypes and desc.returnType.
    ///   3. Recover the std::function via std::any_cast.
    ///   4. Build a vectorised trampoline and register it with the engine.
    [[nodiscard]] virtual std::expected<void, Error> RegisterScalarImpl(ScalarUDFDesc desc)
    {
        (void)desc;
        return std::unexpected(Error{"UDF registration not supported by this adapter"});
    }
};

} // namespace datagrid::adapters
