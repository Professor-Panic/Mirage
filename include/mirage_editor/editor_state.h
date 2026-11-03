#pragma once
#include "entt/entt.hpp"

// Bound to the UI as `mode`. The markup assigns the numbers: Edit = 0, Play = 1.
enum class Mode : int { Edit = 0, Play = 1 };

// State shared by several editor parts. main() creates one and hands a
// reference to each class that needs it.
struct EditorState
{
    entt::entity selected = entt::null;   // bound to the UI as `selected`
    Mode         mode     = Mode::Edit;   // bound to the UI as `mode`
};
