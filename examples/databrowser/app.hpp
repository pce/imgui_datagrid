#pragma once
#include "adapters/filesystem/filesystem_adapter.hpp"
#include "app_config.hpp"
#include "io/drag_drop.hpp"
#include "io/rfs.hpp"
#include "ui/drag_drop_dialog.hpp"
#include "io/image_cache.hpp"
#include "ui/navbar.hpp"
#include "ui/responsive_layout.hpp"
#include "ui/theme.hpp"
#include "ui/theme_customizer.hpp"
#include "ui/text_area.hpp"

#include <array>
#include <filesystem>
#include <optional>
#include <string>

struct sapp_event; // sokol type — global namespace

namespace datagrid {


class App
{
  public:
    std::string dbPath;
    std::string adapterName = std::string{
        adapters::name_of(adapters::AdapterKind::SQLite)};

    void Init();
    void Frame();
    void Cleanup();
    void Event(const sapp_event* ev);

  private:
    void RenderFrame();
    void RenderWelcome();
    void UpdateLayout();
    void TryConnect(const std::string& adapter,
                    const std::string& path,
                    const std::string& passphrase = {},
                    bool               readOnly   = true);
    void InitFsBrowser(const std::string& dirPath);
    void RenderFsNavBar();
    void NavigateFs(std::string absolutePath);
    void InstallImageRenderers(DataBrowser& browser);
    void OpenImageViewer(const std::string& path);
    void OpenSqliteViewer(const std::string& path);
    void OpenTextViewer(const std::filesystem::path& path);
    /// Smart file-open dispatcher: routes to the right viewer based on `how`
    /// ("auto" | "image" | "text" | "sqlite" | "hex" | "system").
    void OpenFsFile(const std::string& path, const std::string& how);
    void RenderImageViewer();
    void RenderTextViewer();
    void SetupDragDrop();
    void SetupDbBrowserDragDrop();

    /// Lazily load the UI (sans) + mono fonts for theme t into the ImGui font atlas.
    /// Safe to call every time a theme is applied — does nothing if already loaded.
    /// sokol_imgui with ImGuiBackendFlags_RendererHasTextures rebuilds the atlas
    /// and uploads the new texture automatically on the next frame.
    void LoadThemeFonts(ui::ThemeStyle t);

    bool        showWelcome_  = true;
    int         adapterIdx_   = 0;
    char        pathBuf_[512] = {};
    std::string connectError_;

    /// Active theme — kept in sync with Navbar and ThemeCustomizer selections.
    ui::ThemeStyle activeTheme_ = ui::ThemeStyle::SolarizedDark;

    /// DPI scale captured at Init(); used by LoadThemeFonts() for lazy loads.
    float dpi_ = 1.0f;

    /// Executable directory — used to resolve Rfs-relative font paths.
    std::filesystem::path exeDir_;

    /// Tracks which themes have had their fonts loaded into the ImGui atlas.
    std::array<bool, ui::kThemeCount> themeFontsLoaded_ = {};

    std::optional<DataBrowser> browser_;
    std::optional<DataBrowser> browser_fs_;
    ui::Navbar                   navbar_;
    ui::Theme                    theme_;
    ui::ThemeCustomizer          customizer_; ///< Owns per-theme palettes; loaded from resources/themes/*.json
    ui::ResponsiveLayout         layout_;
    AppConfig                    config_;

    adapters::FilesystemAdapter* fsAdapter_       = nullptr;
    char                         fsPathBuf_[1024] = {};
    bool                         fsPathSync_      = true;

    bool                        readOnly_ = true;
    bool                        verbose_  = false; ///< Set from DATAGRID_VERBOSE env var; enables startup diagnostics
    char                        passBuf_[256] = {};
    io::ImageCache                  imageCache_;
    std::string                 imageViewerKey_;
    std::string                 imageViewerHint_;
    bool                        showImageViewer_ = false;
    /// Sorted list of sibling image paths (populated when opening an image_path image).
    std::vector<std::string>    imageViewerSiblings_;
    int                         imageViewerSiblingIdx_ = -1;
    ui::TextArea                textViewer_;
    ui::DropDialogManager       dropDialog_;

    /// Resolved file-system — governs which paths are readable/writable/executable.
    /// Mounted in Init():  resources/ → RO,  exe-dir/ → RW.
    io::Rfs                     rfs_;
};
}

