#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <sol/sol.hpp>

// Named events between scripts: the sender does not know who listens and the
// listener does not know who sends. It decouples responsibility, not access:
// any script can emit or listen to any name, so namespace them
// ("enemy.died", not "died").
//
// Lua API (inside a script, and as the global Events for everything else)
// -----------------------------------------------------------------------
//   local id = Events.On("enemy.died", function(data) ... end)   listen
//   Events.Once("level.start", function() ... end)               listen once
//   Events.Off(id)                                               stop listening; true if it existed
//   Events.Emit("enemy.died", { entity = self })                 call every listener NOW, in the
//                                                                order they registered; returns how
//                                                                many ran. Extra arguments are
//                                                                passed on: Emit("hit", a, b)
//   Events.Post("enemy.died", data)                              same, but queued: delivered when
//                                                                the current update tick ends
//
// A listener registered from a script belongs to that script: it is dropped by
// itself when the script is unloaded, reloaded or its entity is destroyed, so a
// dead script is never called. Nothing has to be unregistered by hand (Off is
// only for stopping early).
//
// Emit is safe against re-entrancy: it walks a snapshot of the listeners, so a
// listener may call On / Off / Emit. A listener added while an Emit is running
// hears the NEXT emit, not that one. Nested Emits are cut off at kMaxDepth.
// A listener that errors is reported and the others still run.
class EventBus
{
public:
    // Expires when the script runtime that owns a listener is destroyed.
    using Owner = std::weak_ptr<bool>;

    std::uint64_t On(const std::string& event, sol::protected_function fn,
                     bool owned, Owner owner, bool once)
    {
        if (owned && owner.expired())
            return 0;                       // the script is already gone

        auto handler = std::make_shared<Handler>();
        handler->id    = ++nextId_;
        handler->fn    = std::move(fn);
        handler->owned = owned;
        handler->owner = std::move(owner);
        handler->once  = once;
        handlers_[event].push_back(handler);
        return handler->id;
    }

    bool Off(std::uint64_t id)
    {
        bool found = false;
        for (auto& entry : handlers_)
            for (auto& handler : entry.second)
                if (handler->id == id && !handler->dead)
                {
                    handler->dead = true;
                    found = true;
                }
        return found;
    }

    // Calls every listener now. Returns how many ran.
    int Emit(const std::string& event, const std::vector<sol::object>& args)
    {
        auto it = handlers_.find(event);
        if (it == handlers_.end())
            return 0;

        if (depth_ >= kMaxDepth)
        {
            std::cerr << "Events: '" << event << "' is emitted from inside itself more than "
                      << kMaxDepth << " levels deep; dropped\n";
            return 0;
        }

        // A copy, so listeners may add / remove listeners while this runs.
        const std::vector<std::shared_ptr<Handler>> snapshot = it->second;

        ++depth_;
        int called = 0;
        for (const std::shared_ptr<Handler>& handler : snapshot)
        {
            if (handler->dead)
                continue;
            if (handler->owned && handler->owner.expired())
            {
                handler->dead = true;
                continue;
            }
            if (handler->once)
                handler->dead = true;       // before the call, so a nested Emit cannot fire it twice

            sol::protected_function_result result = handler->fn(sol::as_args(args));
            ++called;

            if (!result.valid())
            {
                sol::error err = result;
                std::cerr << "Events: listener for '" << event << "' failed: "
                          << err.what() << '\n';
            }
        }
        --depth_;

        Prune(event);
        return called;
    }

    // Queues an event for Flush().
    void Post(const std::string& event, std::vector<sol::object> args)
    {
        if (queue_.size() >= kMaxQueued)
        {
            std::cerr << "Events: queue is full; '" << event << "' dropped\n";
            return;
        }
        queue_.push_back({ event, std::move(args) });
    }

    // Delivers what was posted before this call. Events posted by listeners while
    // it runs wait for the next Flush, so a listener that posts its own event
    // cannot loop forever inside one tick.
    void Flush()
    {
        if (queue_.empty())
            return;

        std::deque<Queued> batch;
        batch.swap(queue_);
        for (const Queued& queued : batch)
            Emit(queued.event, queued.args);
    }

    // Forget every listener and queued event (scene switch, Edit <-> Play, shutdown).
    // Must happen before the Lua state is destroyed: the listeners hold Lua references.
    void Clear()
    {
        handlers_.clear();
        queue_.clear();
    }

private:
    struct Handler
    {
        std::uint64_t id = 0;
        sol::protected_function fn;
        bool owned = false;
        Owner owner;
        bool once = false;
        bool dead = false;
    };

    struct Queued
    {
        std::string event;
        std::vector<sol::object> args;
    };

    void Prune(const std::string& event)
    {
        auto it = handlers_.find(event);
        if (it == handlers_.end())
            return;

        auto& list = it->second;
        for (std::size_t i = 0; i < list.size();)
        {
            const Handler& h = *list[i];
            if (h.dead || (h.owned && h.owner.expired()))
                list.erase(list.begin() + static_cast<std::ptrdiff_t>(i));
            else
                ++i;
        }
        if (list.empty())
            handlers_.erase(it);
    }

    static constexpr int         kMaxDepth  = 32;
    static constexpr std::size_t kMaxQueued = 10000;

    std::unordered_map<std::string, std::vector<std::shared_ptr<Handler>>> handlers_;
    std::deque<Queued> queue_;
    std::uint64_t nextId_ = 0;
    int depth_ = 0;
};

// The Lua side. `owned` listeners are tied to `owner` (a script's liveness token);
// the global Events table (not owned) lives until EventBus::Clear().
inline sol::table MakeEventsTable(sol::state_view lua, EventBus& bus,
                                  bool owned, EventBus::Owner owner)
{
    sol::table events = lua.create_table();

    const auto collect = [](sol::variadic_args va)
    {
        std::vector<sol::object> args;
        args.reserve(va.size());
        for (std::size_t i = 0; i < va.size(); ++i)
            args.push_back(va.get<sol::object>(i));
        return args;
    };

    events.set_function("Latch",
        [&bus, owned, owner](const std::string& name, sol::protected_function fn) -> std::uint64_t
        {
            return bus.On(name, std::move(fn), owned, owner, false);
        });

    events.set_function("LatchOnce",
        [&bus, owned, owner](const std::string& name, sol::protected_function fn) -> std::uint64_t
        {
            return bus.On(name, std::move(fn), owned, owner, true);
        });

    events.set_function("Release",
        [&bus](std::uint64_t id) { return bus.Off(id); });

    events.set_function("Emit",
        [&bus, collect](const std::string& name, sol::variadic_args va)
        {
            return bus.Emit(name, collect(va));
        });

    events.set_function("Post",
        [&bus, collect](const std::string& name, sol::variadic_args va)
        {
            bus.Post(name, collect(va));
        });

    return events;
}
