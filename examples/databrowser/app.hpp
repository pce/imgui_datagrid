#pragma once
#include "app_config.hpp"
#include "data_browser.hpp"
#include "navbar.hpp"
#include "responsive_layout.hpp"
#include "theme.hpp"
#include "adapters/filesystem/filesystem_adapter.hpp"

#include <optional>
#include <string>

struct sapp_event;

class App {
public:
    std::string dbPath;
    std::string adapterName = std::string{Adapters::name_of(Adapters::AdapterKind::SQLite)};

    void Init();
    void Frame();
    void Cleanup();
    void Event(const sapp_event* ev);

private:
    void RenderFrame();
    void RenderWelcome();
    void UpdateLayout();
    void TryConnect(const std::string& adapter, const std::string& path,
                    const std::string& passphrase = {},
                    bool               readOnly   = true);
    void InitFsBrowser(const std::string& dirPath);
    void RenderFsNavBar();
    void NavigateFs(const std::string& absolutePath);

    bool showWelcome_  = true;
    int  adapterIdx_   = 0;
    char pathBuf_[512] = {};
    std::string connectError_;

    std::optional<DataBrowser> browser_;
    std::optional<DataBrowser> browser_fs_;
    Navbar           navbar_;
    Theme            theme_;
    ResponsiveLayout layout_;
    AppConfig        config_;

    // ── Filesystem navigation ──────────────────────────────────────────────
    /// Pointer into browser_fs_'s adapter; valid while browser_fs_ exists.
    Adapters::FilesystemAdapter* fsAdapter_ = nullptr;
    char   fsPathBuf_[1024] = {};   ///< Editable path bar buffer for FS nav
    bool   fsPathSync_      = true; ///< When true, sync fsPathBuf_ from adapter

    // ── Welcome dialog ────────────────────────────────────────────────────
    bool   readOnly_        = true;  ///< "Open read-only" toggle in welcome dialog
    char   passBuf_[256]    = {};    ///< Passphrase field (SQLite/SQLCipher)
};
