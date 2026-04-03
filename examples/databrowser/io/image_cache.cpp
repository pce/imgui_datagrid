#include "image_cache.hpp"
#include "stb_image.h"
#include <filesystem>

namespace datagrid::io {

sg_sampler ImageCache::DefaultSampler()
{
    if (sampler_.id == SG_INVALID_ID) {
        sg_sampler_desc d = {};
        d.min_filter      = SG_FILTER_LINEAR;
        d.mag_filter      = SG_FILTER_LINEAR;
        d.mipmap_filter   = SG_FILTER_NEAREST;
        d.wrap_u          = SG_WRAP_CLAMP_TO_EDGE;
        d.wrap_v          = SG_WRAP_CLAMP_TO_EDGE;
        sampler_          = sg_make_sampler(&d);
    }
    return sampler_;
}

ImageCache::Entry ImageCache::MakeFromPixels(const unsigned char* rgba, int w, int h)
{
    sg_image_desc d           = {};
    d.width                   = w;
    d.height                  = h;
    d.pixel_format            = SG_PIXELFORMAT_RGBA8;
    d.data.mip_levels[0].ptr  = rgba;
    d.data.mip_levels[0].size = static_cast<size_t>(w * h * 4);
    sg_image img              = sg_make_image(&d);

    sg_view_desc vd  = {};
    vd.texture.image = img;
    sg_view view     = sg_make_view(&vd);

    return Entry{img, view, w, h};
}

const ImageCache::Entry* ImageCache::GetOrLoad(const std::string& path)
{
    if (const auto it = cache_.find(path); it != cache_.end())
        return &it->second;
    if (!std::filesystem::exists(path))
        return nullptr;
    int      w{}, h{}, ch{};
    stbi_uc* px = stbi_load(path.c_str(), &w, &h, &ch, STBI_rgb_alpha);
    if (!px)
        return nullptr;
    Entry e = MakeFromPixels(px, w, h);
    stbi_image_free(px);
    if (e.handle.id == SG_INVALID_ID)
        return nullptr;
    return &(cache_[path] = std::move(e));
}

const ImageCache::Entry* ImageCache::GetOrLoadMemory(const std::string& key, const unsigned char* data, int byteLen)
{
    if (const auto it = cache_.find(key); it != cache_.end())
        return &it->second;
    int      w{}, h{}, ch{};
    stbi_uc* px = stbi_load_from_memory(data, byteLen, &w, &h, &ch, STBI_rgb_alpha);
    if (!px)
        return nullptr;
    Entry e = MakeFromPixels(px, w, h);
    stbi_image_free(px);
    if (e.handle.id == SG_INVALID_ID)
        return nullptr;
    return &(cache_[key] = std::move(e));
}

const ImageCache::Entry* ImageCache::Get(const std::string& key) const
{
    const auto it = cache_.find(key);
    return it != cache_.end() ? &it->second : nullptr;
}

void ImageCache::Evict(const std::string& key)
{
    if (const auto it = cache_.find(key); it != cache_.end()) {
        sg_destroy_view(it->second.view);
        sg_destroy_image(it->second.handle);
        cache_.erase(it);
    }
}

void ImageCache::Clear()
{
    for (auto& [k, e] : cache_) {
        sg_destroy_view(e.view);
        sg_destroy_image(e.handle);
    }
    cache_.clear();
    if (sampler_.id != SG_INVALID_ID) {
        sg_destroy_sampler(sampler_);
        sampler_ = {SG_INVALID_ID};
    }
}

} // namespace datagrid::io
