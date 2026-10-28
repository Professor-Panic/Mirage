#include "mirage_core/asset_manager.h"
#include <algorithm>

// A small magenta/black checker, made once, on first use (needs the window to exist).
const Texture2D& AssetManager::Placeholder()
{
    if (placeholder_.id == 0)
    {
        Image img = GenImageChecked(16, 16, 8, 8, MAGENTA, BLACK);
        placeholder_ = LoadTextureFromImage(img);
        UnloadImage(img);
    }
    return placeholder_;
}

const Texture2D& AssetManager::Get(const std::string& rawPath)
{
    if (rawPath.empty()) return Placeholder();

    // "a\b.png" and "a/b.png" must be the same entry.
    std::string path = rawPath;
    std::replace(path.begin(), path.end(), '\\', '/');

    // Already loaded.
    auto it = textures_.find(path);
    if (it != textures_.end()) return it->second;

    // Failed before: don't hit the disk or spam the log every frame.
    if (failed_.count(path)) return Placeholder();

    Texture2D tex = LoadTexture(path.c_str());
    if (tex.id == 0)
    {
        failed_.insert(path);
        TraceLog(LOG_WARNING, "[assets] could not load texture '%s'", path.c_str());
        return Placeholder();
    }

    SetTextureFilter(tex, TEXTURE_FILTER_POINT);    // crisp pixels; use TEXTURE_FILTER_BILINEAR for smooth
    return textures_.emplace(path, tex).first->second;
}