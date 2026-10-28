#include "mirage_core/asset_manager.h"
struct Item {
    entt::entity entity;
    int          layer;
    float        z;
};
class SpriteRenderer {
public:
    explicit SpriteRenderer(AssetManager& assets) : assets_(assets) {}
    void Draw(entt::registry& registry);          // one call per frame, inside Begin/EndDrawing
private:
    AssetManager& assets_;
    std::vector<Item> items_;                     // reused each frame, so there's no per-frame allocation
};