#pragma once
#include "adapters/adapter_kind.hpp"
#include "io/file_io.hpp"
#include "ui/theme.hpp"

#include <filesystem>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>


namespace datagrid {
    struct AppConfig
    {
        std::string    adapterName;
        std::string    connectionString;
        ui::ThemeStyle activeTheme = ui::ThemeStyle::SolarizedDark;

        [[nodiscard]] std::optional<adapters::AdapterKind> kind() const noexcept { return adapters::kind_of(adapterName); }
    };

    [[nodiscard]] inline std::filesystem::path AppConfigPath()
    {
        return std::filesystem::path{"databrowser_config.json"};
    }

    [[nodiscard]] inline AppConfig LoadAppConfig()
    {
        AppConfig  cfg;
        const auto path = AppConfigPath();
        const auto text = io::read_text_file(path);
        if (!text) return cfg;
        try {
            const auto j    = nlohmann::json::parse(*text);
            cfg.adapterName      = j.value("adapterName", std::string{});
            cfg.connectionString = j.value("connectionString", std::string{});
            cfg.activeTheme      = ui::theme_from_id(
                j.value("activeTheme", std::string(ui::theme_id(cfg.activeTheme))));
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
            j["activeTheme"]      = std::string(ui::theme_id(cfg.activeTheme));
            const auto content = j.dump(2) + "\n";
            (void)io::write_text_file(AppConfigPath(), content);
        } catch (const std::exception&) {
        }
    }
}