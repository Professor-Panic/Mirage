#pragma once
#include <algorithm>
#include <functional>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "entt.hpp"
#include "entity_id.h"

// ---------------------------------------------------------------------------
// EntityBus
//
// Central service that maps stable Uuids <-> live entt entities.
//
//  - Keeps its index current through on_construct/on_destroy<IdComponent>,
//    so entities added or destroyed anywhere stay consistent.
//  - Resolve() never throws and never returns a stale handle: a missing or
//    destroyed target gives entt::null.
//  - Duplicate or null Uuids are repaired when an IdComponent is added.
//
// Declare it right after the registry so it is destroyed before it.
//
// Scene loading: create each entity with Create(savedUuid) (or SetUuid) first.
// EntityRefs are plain Uuids, so they resolve correctly regardless of the
// order entities were loaded in.
// ---------------------------------------------------------------------------
class EntityBus {
public:
    // Supplies the display name of an entity (e.g. GameComponent::name).
    using NameFn = std::function<std::string(const entt::registry&, entt::entity)>;

    struct Entry {
        EntityRef   ref;
        entt::entity entity;
        std::string name;
    };

    explicit EntityBus(entt::registry& registry) : reg_(registry) {
        reg_.on_construct<IdComponent>().connect<&EntityBus::OnIdAdded>(*this);
        reg_.on_destroy<IdComponent>().connect<&EntityBus::OnIdRemoved>(*this);

        // Adopt anything that already has an id.
        for (auto e : reg_.view<IdComponent>())
            Index(e, reg_.get<IdComponent>(e));
    }

    ~EntityBus() {
        reg_.on_construct<IdComponent>().disconnect<&EntityBus::OnIdAdded>(*this);
        reg_.on_destroy<IdComponent>().disconnect<&EntityBus::OnIdRemoved>(*this);
    }

    EntityBus(const EntityBus&) = delete;
    EntityBus& operator=(const EntityBus&) = delete;

    entt::registry& Registry() const { return reg_; }

    // ----- Identity ---------------------------------------------------------

    // Create an entity with an IdComponent. Pass a Uuid to restore a saved
    // entity. If it collides with a live one a new Uuid is assigned instead,
    // so check IdOf(e) when that matters.
    entt::entity Create(Uuid wanted = {}) {
        entt::entity e = reg_.create();
        reg_.emplace<IdComponent>(e, IdComponent{wanted});
        return e;
    }

    // Give an existing entity an id if it has none. Returns a ref to it.
    EntityRef Register(entt::entity e) {
        if (!reg_.valid(e)) return {};

        if (auto* id = reg_.try_get<IdComponent>(e))
            return EntityRef{id->uuid};

        reg_.emplace<IdComponent>(e);                  // handler assigns the Uuid
        return EntityRef{reg_.get<IdComponent>(e).uuid};
    }

    // Force a specific Uuid (scene loading). False if it's taken by another entity.
    bool SetUuid(entt::entity e, Uuid uuid) {
        if (!reg_.valid(e) || uuid.IsNull()) return false;

        auto taken = byUuid_.find(uuid);
        if (taken != byUuid_.end() && taken->second != e) return false;

        if (auto* id = reg_.try_get<IdComponent>(e)) {
            auto old = byUuid_.find(id->uuid);
            if (old != byUuid_.end() && old->second == e) byUuid_.erase(old);
            id->uuid = uuid;
            byUuid_[uuid] = e;
        } else {
            reg_.emplace<IdComponent>(e, IdComponent{uuid});
        }
        return true;
    }

    // Uuid of an entity, or null if it has none (does not create one).
    Uuid IdOf(entt::entity e) const {
        if (!reg_.valid(e)) return {};
        const auto* id = reg_.try_get<IdComponent>(e);
        return id ? id->uuid : Uuid{};
    }

    // ----- Resolving --------------------------------------------------------

    entt::entity Resolve(const EntityRef& ref) const {
        if (ref.IsNull()) return entt::null;

        auto it = byUuid_.find(ref.id);
        if (it == byUuid_.end() || !reg_.valid(it->second)) return entt::null;

        return it->second;
    }

    bool IsValid(const EntityRef& ref) const { return Resolve(ref) != entt::null; }

    // Component of the referenced entity, or nullptr if the ref is null,
    // missing, destroyed, or the entity lacks C.
    template <class C>
    C* TryGet(const EntityRef& ref) const {
        entt::entity e = Resolve(ref);
        return e == entt::null ? nullptr : reg_.try_get<C>(e);
    }

    bool Destroy(const EntityRef& ref) {
        entt::entity e = Resolve(ref);
        if (e == entt::null) return false;
        reg_.destroy(e);
        return true;
    }

    // ----- Names ------------------------------------------------------------

    void SetNameProvider(NameFn fn) { nameFn_ = std::move(fn); }

    std::string NameOf(entt::entity e) const {
        if (nameFn_) {
            std::string n = nameFn_(reg_, e);
            if (!n.empty()) return n;
        }
        if (const auto* id = reg_.try_get<IdComponent>(e))
            return "Entity " + id->uuid.ToString().substr(0, 8);
        return "Entity";
    }

    // First entity with this name (null ref if none). Only entities that have
    // an IdComponent are searched.
    EntityRef FindByName(const std::string& name) const {
        for (auto e : reg_.view<IdComponent>())
            if (NameOf(e) == name)
                return EntityRef{reg_.get<IdComponent>(e).uuid};
        return {};
    }

    std::vector<EntityRef> FindAllByName(const std::string& name) const {
        std::vector<EntityRef> out;
        for (auto e : reg_.view<IdComponent>())
            if (NameOf(e) == name)
                out.push_back(EntityRef{reg_.get<IdComponent>(e).uuid});
        return out;
    }

    // Every identified entity, sorted by name. Intended for pickers.
    std::vector<Entry> List() const {
        std::vector<Entry> out;
        for (auto e : reg_.view<IdComponent>())
            out.push_back({EntityRef{reg_.get<IdComponent>(e).uuid}, e, NameOf(e)});

        std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) {
            if (a.name != b.name) return a.name < b.name;
            return a.ref.ToString() < b.ref.ToString();
        });
        return out;
    }

    std::size_t Count() const { return byUuid_.size(); }

private:
    void OnIdAdded(entt::registry& r, entt::entity e) { Index(e, r.get<IdComponent>(e)); }

    void OnIdRemoved(entt::registry& r, entt::entity e) {
        auto it = byUuid_.find(r.get<IdComponent>(e).uuid);
        if (it != byUuid_.end() && it->second == e) byUuid_.erase(it);
    }

    // Record e's Uuid, repairing null or duplicate ids first.
    void Index(entt::entity e, IdComponent& id) {
        auto it = byUuid_.find(id.uuid);
        const bool duplicate = it != byUuid_.end() && it->second != e;

        if (id.uuid.IsNull() || duplicate) {
            if (duplicate)
                std::cerr << "EntityBus: duplicate Uuid " << id.uuid.ToString()
                          << ", assigning a new one\n";
            do { id.uuid = Uuid::Generate(); } while (byUuid_.count(id.uuid));
        }

        byUuid_[id.uuid] = e;
    }

    entt::registry& reg_;
    std::unordered_map<Uuid, entt::entity> byUuid_;
    NameFn nameFn_;
};
