#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <random>
#include <string>
#include <string_view>

// ---------------------------------------------------------------------------
// Uuid - 128-bit identifier, formatted like an RFC 4122 v4 UUID.
// All-zero means "null".
// ---------------------------------------------------------------------------
struct Uuid {
    std::uint64_t hi = 0;
    std::uint64_t lo = 0;

    bool IsNull() const { return hi == 0 && lo == 0; }

    friend bool operator==(const Uuid& a, const Uuid& b) { return a.hi == b.hi && a.lo == b.lo; }
    friend bool operator!=(const Uuid& a, const Uuid& b) { return !(a == b); }

    static Uuid Generate() {
        static std::mt19937_64 rng = [] {
            std::random_device rd;
            std::seed_seq seed{rd(), rd(), rd(), rd()};
            return std::mt19937_64(seed);
        }();

        Uuid u;
        do {
            u.hi = rng();
            u.lo = rng();
        } while (u.IsNull());

        // Version 4 / RFC 4122 variant bits.
        u.hi = (u.hi & 0xFFFFFFFFFFFF0FFFull) | 0x0000000000004000ull;
        u.lo = (u.lo & 0x3FFFFFFFFFFFFFFFull) | 0x8000000000000000ull;
        return u;
    }

    // "" for null, otherwise 8-4-4-4-12 lowercase hex.
    std::string ToString() const {
        if (IsNull()) return {};

        char buf[37];
        std::snprintf(buf, sizeof buf, "%08x-%04x-%04x-%04x-%012llx",
                      static_cast<unsigned>(hi >> 32),
                      static_cast<unsigned>((hi >> 16) & 0xFFFF),
                      static_cast<unsigned>(hi & 0xFFFF),
                      static_cast<unsigned>(lo >> 48),
                      static_cast<unsigned long long>(lo & 0xFFFFFFFFFFFFull));
        return buf;
    }

    // Accepts dashed or plain 32-digit hex. Anything else yields a null Uuid.
    static Uuid Parse(std::string_view s) {
        Uuid u;
        int digits = 0;

        for (char c : s) {
            if (c == '-') continue;

            int d;
            if (c >= '0' && c <= '9')      d = c - '0';
            else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else return {};

            if (digits >= 32) return {};

            std::uint64_t& half = digits < 16 ? u.hi : u.lo;
            half = (half << 4) | static_cast<std::uint64_t>(d);
            ++digits;
        }

        return digits == 32 ? u : Uuid{};
    }
};

namespace std {
template <>
struct hash<Uuid> {
    size_t operator()(const Uuid& u) const noexcept {
        return static_cast<size_t>(u.hi ^ (u.lo * 0x9E3779B97F4A7C15ull));
    }
};
}

// ---------------------------------------------------------------------------
// IdComponent - the persistent identity of an entity.
// Separate from entt::entity, which is only valid for this run.
// Never write `uuid` directly; use EntityBus::SetUuid so the index stays in sync.
// ---------------------------------------------------------------------------
struct IdComponent {
    Uuid uuid;
};

// ---------------------------------------------------------------------------
// EntityRef - a reference to an entity by Uuid.
//
// Safe to store in components / script fields / scene files:
//  - it never holds an entt handle, so a recycled handle can't alias it
//  - it can point at an entity that is loaded later (resolve on use)
//  - a destroyed target just resolves to "missing"
// ---------------------------------------------------------------------------
struct EntityRef {
    Uuid id;

    bool IsNull() const { return id.IsNull(); }

    std::string ToString() const { return id.ToString(); }
    static EntityRef FromString(std::string_view s) { return EntityRef{Uuid::Parse(s)}; }

    friend bool operator==(const EntityRef& a, const EntityRef& b) { return a.id == b.id; }
    friend bool operator!=(const EntityRef& a, const EntityRef& b) { return !(a == b); }
};
