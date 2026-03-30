#pragma once
#include "data_source.hpp"
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Adapters {

using MakeAdapterFn = DataSourcePtr (*)();

struct AdapterEntry {
    std::string   name;
    MakeAdapterFn make;
};

class AdapterRegistry {
public:
    static void Register(std::string_view name, MakeAdapterFn make);

    [[nodiscard]] static DataSourcePtr Create(std::string_view name);

    [[nodiscard]] static std::expected<DataSourcePtr, std::string>
        CreateConnected(std::string_view name, const ConnectionParams& params);

    [[nodiscard]] static bool Has(std::string_view name) noexcept;

    [[nodiscard]] static std::span<const AdapterEntry> Entries() noexcept;

    [[nodiscard]] static std::vector<std::string> RegisteredAdapters();

    [[nodiscard]] static int Count() noexcept;

private:
    AdapterRegistry() = delete;
    static std::vector<AdapterEntry>& Table() noexcept;
};

template <typename Derived>
struct AutoRegister {
    static const bool registered_;
    static bool DoRegister() {
        AdapterRegistry::Register(
            Derived::AdapterKey,
            +[]() -> DataSourcePtr { return std::make_unique<Derived>(); }
        );
        return true;
    }
};

template <typename Derived>
const bool AutoRegister<Derived>::registered_ = AutoRegister<Derived>::DoRegister();

}

#define REGISTER_ADAPTER_IMPL(Counter, AdapterClass, AdapterName)        \
    namespace {                                                          \
        static const bool _auto_reg_##Counter = []() -> bool {           \
            ::Adapters::AdapterRegistry::Register(                       \
                AdapterName,                                             \
                +[]() -> ::Adapters::DataSourcePtr {                     \
                    return std::make_unique<AdapterClass>();              \
                }                                                        \
            );                                                           \
            return true;                                                 \
        }();                                                             \
    }

#define REGISTER_ADAPTER(AdapterClass, AdapterName) \
    REGISTER_ADAPTER_IMPL(__COUNTER__, AdapterClass, AdapterName)
