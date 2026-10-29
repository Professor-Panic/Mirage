#include "mirage_core/asset_manager.h"
#include "mirage_core/components.h"   // SpriteComponent
#include <algorithm>

namespace {
// "a\\b.png" and "a/b.png" are the same entry.
std::string Normalize(std::string path)
{
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}
}  // namespace

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
    const std::string path = Normalize(rawPath);

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
// Loads every texture the scene's sprites use, so the first frame doesn't hitch.
void AssetManager::Preload(entt::registry& reg)
{
    for (auto [entity, sprite] : reg.view<SpriteComponent>().each())
        Get(sprite.path);
}

// Re-reads one file from disk (after you edited the image). A path that was never
// loaded, or that failed before, is simply tried again the next time it is drawn.
void AssetManager::Reload(const std::string& rawPath)
{
    const std::string path = Normalize(rawPath);
    failed_.erase(path);

    auto it = textures_.find(path);
    if (it == textures_.end()) return;

    Texture2D fresh = LoadTexture(path.c_str());
    if (fresh.id == 0)
    {
        TraceLog(LOG_WARNING, "[assets] reload of '%s' failed, keeping the old texture", path.c_str());
        return;
    }

    SetTextureFilter(fresh, TEXTURE_FILTER_POINT);
    UnloadTexture(it->second);
    it->second = fresh;                    // same map entry, so references stay valid
}

void AssetManager::UnloadAll()
{
    for (auto& [path, tex] : textures_) UnloadTexture(tex);
    textures_.clear();
    failed_.clear();

    if (placeholder_.id != 0)
    {
        UnloadTexture(placeholder_);
        placeholder_ = {};
    }
}