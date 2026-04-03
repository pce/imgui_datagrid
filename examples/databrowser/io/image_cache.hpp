#pragma once
#include "sokol_gfx.h"
#include <string>
#include <unordered_map>

/// GPU-side image cache. Maps a string key -> sg_image + sg_view + pixel dimensions.
///
/// "image_path" columns  -> key = absolute filesystem path
/// "image_blob" columns  -> key = "colKey.rowIndex"
///
/// All sg_* calls require sg_setup() to have already run.
/// Call Clear() before sg_shutdown().

namespace datagrid::io {

class ImageCache
{
  public:
    struct Entry
    {
        sg_image handle = {SG_INVALID_ID};
        sg_view  view   = {SG_INVALID_ID};
        int      w      = 0;
        int      h      = 0;
    };

    ImageCache() = default;
    ~ImageCache() { Clear(); }
    ImageCache(const ImageCache&)            = delete;
    ImageCache& operator=(const ImageCache&) = delete;

    /// Load from a filesystem path. Returns nullptr on failure.
    [[nodiscard]] const Entry* GetOrLoad(const std::string& path);

    /// Load from raw bytes (e.g. SQLite BLOB). key is a stable cache identifier.
    [[nodiscard]] const Entry* GetOrLoadMemory(const std::string& key, const unsigned char* data, int byteLen);

    /// Cache-only lookup, never triggers a load. Returns nullptr on miss.
    [[nodiscard]] const Entry* Get(const std::string& key) const;

    void Evict(const std::string& key);
    void Clear(); ///< Must be called before sg_shutdown()

    /// Linear clamp-to-edge sampler, created lazily on first use.
    [[nodiscard]] sg_sampler DefaultSampler();

  private:
    [[nodiscard]] Entry MakeFromPixels(const unsigned char* rgba, int w, int h);

    std::unordered_map<std::string, Entry> cache_;
    sg_sampler                             sampler_ = {SG_INVALID_ID};
};

} // namespace datagrid::io
