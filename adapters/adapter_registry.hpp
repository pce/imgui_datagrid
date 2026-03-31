#pragma once
#include "data_source.hpp"
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Adapters {

using MakeAdapterFn = DataSourcePtr (*)();

struct AdapterEntry
{
    std::string   name;
    MakeAdapterFn make;
};

class AdapterRegistry
{
  public:
    static void Register(std::string_view name, MakeAdapterFn make);

    [[nodiscard]] static DataSourcePtr Create(std::string_view name);

    [[nodiscard]] static std::expected<DataSourcePtr, std::string> CreateConnected(std::string_view        name,
                                                                                   const ConnectionParams& params);

    [[nodiscard]] static bool Has(std::string_view name) noexcept;

    [[nodiscard]] static std::span<const AdapterEntry> Entries() noexcept;

    [[nodiscard]] static std::vector<std::string> RegisteredAdapters();

    [[nodiscard]] static int Count() noexcept;

  private:
    AdapterRegistry() = delete;
    static std::vector<AdapterEntry>& Table() noexcept;
};

// Declare one namespace-scope static per adapter .cpp to register it with
// AdapterRegistry before main() runs:
//
//   namespace {
//       const Adapters::RegisterAdapter<MyAdapter> kReg{ "myadapter" };
//   }
//
// The non-capturing lambda converts to a raw function pointer via the unary
// `+` operator — zero heap allocation, zero std::function overhead.
//
// RegisterAdapter is non-copyable and non-movable; it is only meaningful as
// a namespace-scope static with static storage duration.
template<typename TAdapter>
struct RegisterAdapter
{
    explicit RegisterAdapter(std::string_view name) noexcept
    {
        AdapterRegistry::Register(name, +[]() -> DataSourcePtr { return std::make_unique<TAdapter>(); });
    }
    RegisterAdapter(const RegisterAdapter&)            = delete;
    RegisterAdapter& operator=(const RegisterAdapter&) = delete;
};

} // namespace Adapters
