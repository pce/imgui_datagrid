#include "adapter_registry.hpp"
#include <algorithm>
#include <format>
#include <ranges>

namespace Adapters {

std::vector<AdapterEntry>& AdapterRegistry::Table() noexcept
{
    static std::vector<AdapterEntry> t;
    return t;
}

void AdapterRegistry::Register(std::string_view name, MakeAdapterFn make)
{
    auto& t = Table();
    if (auto it = std::ranges::find(t, name, &AdapterEntry::name); it != t.end())
        it->make = make;
    else {
        t.push_back({std::string{name}, make});
        std::ranges::sort(t, {}, &AdapterEntry::name);
    }
}

DataSourcePtr AdapterRegistry::Create(std::string_view name)
{
    const auto& t  = Table();
    const auto  it = std::ranges::lower_bound(t, name, std::ranges::less{}, &AdapterEntry::name);
    return (it != t.end() && it->name == name) ? it->make() : nullptr;
}

std::expected<DataSourcePtr, std::string> AdapterRegistry::CreateConnected(std::string_view        name,
                                                                           const ConnectionParams& params)
{
    auto ds = Create(name);
    if (!ds) {
        std::string reg;
        for (const auto& e : Table()) {
            if (!reg.empty())
                reg += ", ";
            reg += e.name;
        }
        return std::unexpected(
            std::format("Unknown adapter \"{}\". Registered: {}", name, reg.empty() ? "(none)" : reg));
    }
    return ds->Connect(params).transform([ds = std::move(ds)]() mutable -> DataSourcePtr { return std::move(ds); });
}

bool AdapterRegistry::Has(std::string_view name) noexcept
{
    const auto& t  = Table();
    const auto  it = std::ranges::lower_bound(t, name, std::ranges::less{}, &AdapterEntry::name);
    return it != t.end() && it->name == name;
}

std::span<const AdapterEntry> AdapterRegistry::Entries() noexcept
{
    return Table();
}

std::vector<std::string> AdapterRegistry::RegisteredAdapters()
{
    std::vector<std::string> names;
    names.reserve(Table().size());
    for (const auto& e : Table())
        names.push_back(e.name);
    return names;
}

int AdapterRegistry::Count() noexcept
{
    return static_cast<int>(Table().size());
}

} // namespace Adapters
