#pragma once
#include "adapters/filesystem/filesystem_adapter.hpp"
#include "app_config.hpp"
#include "data_browser.hpp"
#include "drag_drop.hpp"
#include "ui/drag_drop_dialog.hpp"
#include "image_cache.hpp"
#include "ui/navbar.hpp"
#include "ui/responsive_layout.hpp"
#include "theme.hpp"
#include "ui/text_area.hpp"

#include <optional>
#include <string>

struct sapp_event;

class App
{
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

    bool        showWelcome_  = true;
    int         adapterIdx_   = 0;
    char        pathBuf_[512] = {};
    std::string connectError_;

    std::optional<DataBrowser> browser_;
    std::optional<DataBrowser> browser_fs_;
    UI::Navbar                     navbar_;
    Theme                      theme_;
    UI::ResponsiveLayout           layout_;
    AppConfig                  config_;

    Adapters::FilesystemAdapter* fsAdapter_       = nullptr;
    char                         fsPathBuf_[1024] = {};
    bool                         fsPathSync_      = true;

    bool                        readOnly_ = true;
    bool                        verbose_  = false; ///< Set from DATAGRID_VERBOSE env var; enables startup diagnostics
    char                        passBuf_[256] = {};
    ImageCache                  imageCache_;
    std::string                 imageViewerKey_;
    std::string                 imageViewerHint_;
    bool                        showImageViewer_ = false;
    /// Sorted list of sibling image paths (populated when opening an image_path image).
    std::vector<std::string>    imageViewerSiblings_;
    int                         imageViewerSiblingIdx_ = -1;
    UI::TextArea                textViewer_;
    UI::DropDialogManager dropDialog_;

};
