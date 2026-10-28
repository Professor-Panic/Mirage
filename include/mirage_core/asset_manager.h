#pragma once
#include "raylib.h"
#include "entt/entt.hpp"
#include <map>
#include <set>
#include <string>                                     // was missing

class AssetManager {
public:
    AssetManager() = default;
    ~AssetManager() { UnloadAll(); }                  // safe even if already called
    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    const Texture2D& Get(const std::string& path);

    void Preload(entt::registry& reg);
    void Reload(const std::string& path);
    void UnloadAll();                                 // call BEFORE CloseWindow()

private:
    const Texture2D& Placeholder();                   // new

    std::map<std::string, Texture2D> textures_;
    Texture2D placeholder_{};
    std::set<std::string> failed_;                    // renamed from warned_
};