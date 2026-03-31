#pragma once
#include "adapters/adapter_kind.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

struct AppConfig
{
    std::string adapterName;
    std::string connectionString;

    [[nodiscard]] std::optional<Adapters::AdapterKind> kind() const noexcept { return Adapters::kind_of(adapterName); }
};

[[nodiscard]] inline std::filesystem::path AppConfigPath()
{
    return std::filesystem::path("databrowser_config.json");
}

[[nodiscard]] inline AppConfig LoadAppConfig()
{
    AppConfig  cfg;
    const auto path = AppConfigPath();
    if (!std::filesystem::exists(path))
        return cfg;
    try {
        std::ifstream f(path);
        if (!f.is_open())
            return cfg;
        const auto j         = nlohmann::json::parse(f);
        cfg.adapterName      = j.value("adapterName", std::string{});
        cfg.connectionString = j.value("connectionString", std::string{});
    } catch (const std::exception&) {
    }
    return cfg;
}

inline void SaveAppConfig(const AppConfig& cfg)
{
    try {
        nlohmann::json j;
        j["adapterName"]      = cfg.adapterName;
        j["connectionString"] = cfg.connectionString;
        std::ofstream f(AppConfigPath());
        if (f.is_open())
            f << j.dump(2) << '\n';
    } catch (const std::exception&) {
    }
}
