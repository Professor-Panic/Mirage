#pragma once
#include <vector>

#include "entt/entt.hpp"
#include "mirage_core/asset_manager.h"

class SpriteRenderer {
public:
    explicit SpriteRenderer(AssetManager& assets) : assets_(assets) {}
    void Draw(entt::registry& registry,int gridX,int gridY);          // one call per frame, inside Begin/EndDrawing
    entt::entity Pick(entt::registry& registry, Vector2 worldPx, int grid);

private:
    struct DrawItem {
        entt::entity entity;
        int          layer;
        float        z;
    };

    AssetManager& assets_;
    void CollectSorted(entt::registry& registry); //Collects all visible items
    std::vector<DrawItem> items_;                 // reused each frame, so there's no per-frame allocation
};
