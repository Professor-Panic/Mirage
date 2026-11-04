#pragma once
#include "entity_bus.h"
#include "mirage_core/reflection.h"
#include "mirage_core/entity_bus.h"
#include "mirage_core/script_log.h"
#include "mirage_core/event_bus.h"
#include "mirage_core/script_system.h"
#include "script_system.h"
#include "sol/sol.hpp"
#include "entt/entt.hpp"
struct MirageCoreRuntime{
    Reflection reflection;
    entt::registry registry;
    EntityBus entityBus;
    ScriptLog scriptLog;
    sol::state lua;
    EventBus events;                 // after lua: its listeners hold Lua references and must die first
    ScriptSystem scriptSystem;
    MirageCoreRuntime();
    ~MirageCoreRuntime();
    MirageCoreRuntime(const MirageCoreRuntime&) = delete;
    MirageCoreRuntime& operator=(const MirageCoreRuntime&) = delete;
};