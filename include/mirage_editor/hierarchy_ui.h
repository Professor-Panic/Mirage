#pragma once
#include <string>
#include <vector>

#include "rui.h"
#include "entt/entt.hpp"
#include "mirage_core/components.h"
#include "mirage_core/runtime.h"
#include "mirage_editor/editor_state.h"
#include "mirage_editor/inspector_ui.h"   // ComponentWatch

// One row per entity that has a GameComponent.
struct HierarchyRow
{
    entt::entity id;
    std::string  uuid;   // stable id the hierarchy hands to a drop target
};

// The hierarchy list ("GameComponents") and the delete button.
class HierarchyUi
{
public:
    HierarchyUi(rui::Context& ui, MirageCoreRuntime& core, EditorState& state)
        : core_(core),
          state_(state),
          watch_(core.registry, ui.runtime, "GameComponents")
    {
        ui.runtime.Function("deleteEntity", [this, &ui]()
        {
            core_.entityBus.Destroy(
                core_.entityBus.FindByName(core_.entityBus.NameOf(state_.selected)));
            ui.RequestRebuild();
        });

        ui.runtime.Function("refreshUI", [&ui]()
        {
            ui.runtime.Invalidate("GameComponents");
        });

        ui.runtime.BindList(
            "GameComponents",
            [this]()
            {
                std::vector<HierarchyRow> rows;

                for (auto entity : core_.registry.view<GameComponent>())
                    rows.push_back({entity, core_.entityBus.IdOf(entity).ToString()});

                return rows;
            },
            [](auto& list)
            {
                list.Field("id", &HierarchyRow::id)
                    .Field("uuid", &HierarchyRow::uuid);
            }
        )
        .Via([this](HierarchyRow& row) -> GameComponent&
        {
            return core_.registry.get<GameComponent>(row.id);
        })
        .Ref("name", &GameComponent::name);
    }

    HierarchyUi(const HierarchyUi&) = delete;
    HierarchyUi& operator=(const HierarchyUi&) = delete;

private:
    MirageCoreRuntime&              core_;
    EditorState&                    state_;
    ComponentWatch<GameComponent>   watch_;   // keeps the list fresh when entities change
};
