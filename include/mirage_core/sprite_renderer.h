#pragma once
#include <vector>

#include "entt/entt.hpp"
#include "mirage_core/asset_manager.h"

class SpriteRenderer {
public:
    explicit SpriteRenderer(AssetManager& assets) : assets_(assets) {}
    void Draw(entt::registry& registry);          // one call per frame, inside Begin/EndDrawing

private:
    struct DrawItem {
        entt::entity entity;
        int          layer;
        float        z;
    };

    AssetManager& assets_;
    std::vector<DrawItem> items_;                 // reused each frame, so there's no per-frame allocation
};
