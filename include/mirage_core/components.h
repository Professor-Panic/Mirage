#pragma once
#include "raylib.h"
#include "raymath.h"
#include "string"
struct TransformComponent {
    Vector3 position;
    Vector3 scale;
    Quaternion rotation;

    TransformComponent()
        : position(Vector3Zero()),
          scale(Vector3One()),
          rotation(QuaternionIdentity()) {}
};

struct GameComponent {
    std::string name;

    GameComponent(std::string name) : name(std::move(name)) {}
};
struct SpriteComponent {
    std::string path;                           // texture file, relative to the project root
    Color   tint    = WHITE;
    Vector2 pivot   = { 0.5f, 0.5f };           // 0..1 of the sprite's size; (0.5, 0.5) = centre
    Vector4 source  = { 0.0f, 0.0f, 0.0f, 0.0f }; // x, y, w, h in pixels; all zero = whole texture
    bool    flipX   = false;
    bool    flipY   = false;
    bool    visible = true;
    int     layer   = 1;                        // lower draws first (behind)
};
