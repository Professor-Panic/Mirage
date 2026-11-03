#pragma once
#include "entt/entt.hpp"

// Bound to the UI as `mode`. The markup assigns the numbers: Edit = 0, Play = 1.
enum class Mode : int { Edit = 0, Play = 1 };
enum class Tool { Move, Rotate, Scale };
// State shared by several editor parts. main() creates one and hands a
// reference to each class that needs it.
struct EditorState
{
    entt::entity selected = entt::null;   // bound to the UI as `selected`
    Mode         mode     = Mode::Edit;   // bound to the UI as `mode`
    Tool         tool     = Tool::Move;   // bound to the UI as `tool`: 0 = Move, 1 = Rotate, 2 = Scale
};
