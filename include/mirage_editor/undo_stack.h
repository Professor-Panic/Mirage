#pragma once
#include <deque>
#include "raylib.h"
#include "mirage_core/entity_id.h"  

// Everything a gizmo can change on a transform. One snapshot before a drag and
// one after = one undo step, whichever tool made it.
struct TransformSnapshot {
    Vector3    position;
    Vector3    scale;
    Quaternion rotation;
    Vector3    euler;      // TransformComponent::eulerHint, so undo restores what the inspector showed
};

// One finished drag. Keyed by uuid, not entt::entity, so undo still works
// after deletes and scene reloads.
struct TransformCommand {
    Uuid              id;
    TransformSnapshot before;
    TransformSnapshot after;
};

class UndoStack {
public:
    static constexpr size_t kMax = 500;

    void Push(const TransformCommand& c) {
        undo_.push_back(c);
        if (undo_.size() > kMax) undo_.pop_front();   // drop the oldest
        redo_.clear();                                // a new action ends the redo chain
    }
    bool CanUndo() const { return !undo_.empty(); }
    bool CanRedo() const { return !redo_.empty(); }

    TransformCommand PopUndo() { auto c = undo_.back(); undo_.pop_back(); redo_.push_back(c); return c; }
    TransformCommand PopRedo() { auto c = redo_.back(); redo_.pop_back(); undo_.push_back(c); return c; }

    void Clear() { undo_.clear(); redo_.clear(); }    // call when a scene loads

private:
    std::deque<TransformCommand> undo_, redo_;
};