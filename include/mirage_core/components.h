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
struct SpriteComponent{
    std::string path;
    Color tint;
    Vector2 pivot;
    Vector4 source;
    bool flipX;
    bool flipY;
    bool visible;
    int layer;
    SpriteComponent():tint(WHITE),layer(1),pivot(Vector2(0.5,0.5)){};
};