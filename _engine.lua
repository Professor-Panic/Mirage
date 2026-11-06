---@meta
--- Generated from Mirage ScriptApiRegistry. Do not edit by hand.
--- Regenerate with: Mirage.exe --generate-lua-api [output-path]

---@alias MirageTable1 {x: number, y: number}
---@alias MirageTable2 {x: number, y: number, z: number}

--- Box collider attached to the entity's rigid body (or a static body if it has none).
---@class BoxCollider2D: ComponentHandle
---Ownership: engine-owned
---Lifetime: The component belongs to an EnTT entity. Lua handles resolve it on each access and become unavailable when the entity or component is removed.
---@field size Vector2 # Full width and height in world units, multiplied by the transform's scale.
---@field offset Vector2 # Centre of the box relative to the entity's position.
---@field isSensor boolean # Detects overlaps but causes no physical response.
---@field density number # Mass per area. Zero on a dynamic body means it has no mass.
---@field friction number # Surface friction, usually 0 to 1.
---@field restitution number # Bounciness. 0 = no bounce, 1 = perfectly elastic.
BoxCollider2D = {}

--- Capsule collider attached to the entity's rigid body (or a static body if it has none).
---@class CapsuleCollider2D: ComponentHandle
---Ownership: engine-owned
---Lifetime: The component belongs to an EnTT entity. Lua handles resolve it on each access and become unavailable when the entity or component is removed.
---@field radius number # Radius of the two end caps in world units.
---@field height number # Distance between the two cap centres; total length is height + 2 * radius.
---@field horizontal boolean # True lays the capsule on its side.
---@field offset Vector2 # Centre of the capsule relative to the entity's position.
---@field isSensor boolean # Detects overlaps but causes no physical response.
---@field density number # Mass per area. Zero on a dynamic body means it has no mass.
---@field friction number # Surface friction, usually 0 to 1.
---@field restitution number # Bounciness. 0 = no bounce, 1 = perfectly elastic.
CapsuleCollider2D = {}

--- Circle collider attached to the entity's rigid body (or a static body if it has none).
---@class CircleCollider2D: ComponentHandle
---Ownership: engine-owned
---Lifetime: The component belongs to an EnTT entity. Lua handles resolve it on each access and become unavailable when the entity or component is removed.
---@field radius number # Radius in world units, multiplied by the larger axis of the transform's scale.
---@field offset Vector2 # Centre of the circle relative to the entity's position.
---@field isSensor boolean # Detects overlaps but causes no physical response.
---@field density number # Mass per area. Zero on a dynamic body means it has no mass.
---@field friction number # Surface friction, usually 0 to 1.
---@field restitution number # Bounciness. 0 = no bounce, 1 = perfectly elastic.
CircleCollider2D = {}

--- RGBA color. Channels are integers clamped to the range 0..255.
---@class Color
---Ownership: value
---@field r integer # Red channel
---@field g integer # Green channel
---@field b integer # Blue channel
---@field a integer # Alpha channel
---@operator eq(Color): boolean
---@operator tostring: string
---@overload fun(): Color
---@overload fun(r: number, g: number, b: number): Color
---@overload fun(r: number, g: number, b: number, a: number): Color
Color = {}

--- Create opaque white
---@overload fun(r: number, g: number, b: number): Color
---@overload fun(r: number, g: number, b: number, a: number): Color
---@return Color
function Color.new() end

--- Return a copy of this color
---@return Color
function Color:Copy() end

--- Interpolate to another color
---@param other Color
---@param amount number
---@return Color
function Color:Lerp(other, amount) end

--- Return a copy with a different alpha channel
---@param alpha number
---@return Color
function Color:WithAlpha(alpha) end

--- Opaque live handle to a script-exposed component. Prefer the concrete component type returned by GetComponent with a literal component name.
---@class ComponentHandle
---Ownership: engine-owned
---Lifetime: The handle resolves its entity and component on each access. It becomes invalid when the entity or component is removed.
ComponentHandle = {}

--- Stable reference to an entity. A reference remains safe to hold after its entity is destroyed.
---@class EntityRef
---Ownership: value
---Lifetime: EntityRef stores a stable UUID rather than an EnTT component pointer; IsValid checks whether it currently resolves.
---@operator eq(EntityRef): boolean
---@operator tostring: string
---@overload fun(): EntityRef
EntityRef = {}

--- Create an empty entity reference
---@return EntityRef
function EntityRef.new() end

--- Add a script-exposed component using its reflected defaults
---@param name string
---@return boolean
function EntityRef:AddComponent(name) end

--- Attach another script to this entity
---@param path string
---@return boolean
function EntityRef:AddScript(path) end

--- Add a tag to this entity
---@param tag string
---@return boolean
function EntityRef:AddTag(tag) end

--- Clone this entity and its reflected component/script data
---@return EntityRef|nil
function EntityRef:Clone() end

--- Destroy this entity
---@return boolean
function EntityRef:Destroy() end

--- Get a live native handle to a component, or nil
---@generic T: ComponentHandle
---@param name `T`
---@return T|nil
function EntityRef:GetComponent(name) end

--- Return the entity UUID as a string
---@return string
function EntityRef:GetId() end

--- Return the entity name, or nil when it no longer exists
---@return string|nil
function EntityRef:GetName() end

--- Get the loaded script instance, optionally selecting a script path
---@param path? string
---@return table|nil
function EntityRef:GetScript(path) end

--- Return the entity's tags as an array of strings
---@return string[]
function EntityRef:GetTags() end

--- Check whether the entity has a script-exposed component
---@param name string
---@return boolean
function EntityRef:HasComponent(name) end

--- Check whether a script is attached
---@param path string
---@return boolean
function EntityRef:HasScript(path) end

--- Check whether this entity has a tag
---@param tag string
---@return boolean
function EntityRef:HasTag(tag) end

--- Return whether this reference is empty
---@return boolean
function EntityRef:IsNull() end

--- Return whether the referenced entity currently exists
---@return boolean
function EntityRef:IsValid() end

--- Remove a component from this entity
---@param name string
---@return boolean
function EntityRef:RemoveComponent(name) end

--- Detach a script from this entity
---@param path string
---@return boolean
function EntityRef:RemoveScript(path) end

--- Remove a tag from this entity
---@param tag string
---@return boolean
function EntityRef:RemoveTag(tag) end

--- Replace the entity's scripts with the specified script
---@param path string
---@return boolean
function EntityRef:SetScript(path) end

--- Entity display name and game-facing identity data.
---@class Game: ComponentHandle
---Ownership: engine-owned
---Lifetime: The component belongs to an EnTT entity. Lua handles resolve it on each access and become unavailable when the entity or component is removed.
---@field name string # Display name used to identify the entity in the game.
Game = {}

--- Box2D rigid body settings and live velocity.
---@class RigidBody2D: ComponentHandle
---Ownership: engine-owned
---Lifetime: The component belongs to an EnTT entity. Lua handles resolve it on each access and become unavailable when the entity or component is removed.
---@field type integer # Body type: 0 = Dynamic, 1 = Kinematic, 2 = Static.
---@field mass number # Manual mass. Only used while the entity has no colliders.
---@field gravityScale number # Multiplier on world gravity. 0 = floats, negative = falls upward.
---@field linearDamping number # Drag on linear velocity.
---@field angularDamping number # Drag on rotation.
---@field fixedRotation boolean # Locks rotation so the body never spins.
---@field allowSleep boolean # Lets the body sleep when it comes to rest.
---@field bullet boolean # Continuous collision for fast bodies, so they don't tunnel.
---@field enabled boolean # False removes the body from the simulation.
---@field linearVelocity Vector2 # Units per second. Before the body exists this is the start velocity.
---@field angularVelocity number # Degrees per second.
RigidBody2D = {}

--- Attached script paths and their persisted exposed fields.
---@class Script: ComponentHandle
---Ownership: engine-owned
---Lifetime: The component belongs to an EnTT entity. Lua handles resolve it on each access and become unavailable when the entity or component is removed.
Script = {}

--- Sprite texture, color, source rectangle and rendering settings.
---@class Sprite: ComponentHandle
---Ownership: engine-owned
---Lifetime: The component belongs to an EnTT entity. Lua handles resolve it on each access and become unavailable when the entity or component is removed.
---@field path string # Texture path relative to the project root.
---@field tint Color # Color tint multiplied with the sprite texture.
---@field pivot Vector2 # Normalized sprite origin, where 0.5, 0.5 is the center.
---@field source Vector4 # Texture source rectangle in pixels; zero dimensions use the full texture.
---@field flipX boolean # Whether the sprite is flipped horizontally.
---@field flipY boolean # Whether the sprite is flipped vertically.
---@field visible boolean # Whether the sprite is rendered.
---@field layer integer # Render layer; lower values are drawn behind higher values.
Sprite = {}

--- Comma-separated entity tags, used by Entities.FindWithTag.
---@class Tags: ComponentHandle
---Ownership: engine-owned
---Lifetime: The component belongs to an EnTT entity. Lua handles resolve it on each access and become unavailable when the entity or component is removed.
---@field tags string # Comma-separated list of tags assigned to the entity.
Tags = {}

--- Position, scale and rotation data for an entity transform.
---@class Transform: ComponentHandle
---Ownership: engine-owned
---Lifetime: The component belongs to an EnTT entity. Lua handles resolve it on each access and become unavailable when the entity or component is removed.
---@field position Vector3 # World-space position of the entity.
---@field scale Vector3 # Local scale of the entity.
---@field rotation Vector4 # Stored quaternion rotation.
---@field eulerAngles Vector3 # Rotation in Euler degrees.
Transform = {}

--- Vector2 value type. Supports component-wise arithmetic with other vectors or numbers.
---@class Vector2
---Ownership: value
---@field x number # X component
---@field y number # Y component
---@operator add(Vector2|number): Vector2
---@operator sub(Vector2|number): Vector2
---@operator mul(Vector2|number): Vector2
---@operator div(Vector2|number): Vector2
---@operator unm: Vector2
---@operator eq(Vector2): boolean
---@operator tostring: string
---@overload fun(): Vector2
---@overload fun(x: number, y: number): Vector2
Vector2 = {}

--- Create a zero-initialized value
---@overload fun(x: number, y: number): Vector2
---@return Vector2
function Vector2.new() end

--- Return a copy of this vector
---@return Vector2
function Vector2:Copy() end

--- Return the distance to another vector
---@param other Vector2
---@return number
function Vector2:Distance(other) end

--- Return the dot product with another vector
---@param other Vector2
---@return number
function Vector2:Dot(other) end

--- Return the vector magnitude
---@return number
function Vector2:Length() end

--- Return the squared vector magnitude
---@return number
function Vector2:LengthSqr() end

--- Linearly interpolate to another vector
---@param other Vector2
---@param amount number
---@return Vector2
function Vector2:Lerp(other, amount) end

--- Return a normalized copy; zero remains zero
---@return Vector2
function Vector2:Normalize() end

--- Vector3 value type. Supports component-wise arithmetic with other vectors or numbers.
---@class Vector3
---Ownership: value
---@field x number # X component
---@field y number # Y component
---@field z number # Z component
---@operator add(Vector3|number): Vector3
---@operator sub(Vector3|number): Vector3
---@operator mul(Vector3|number): Vector3
---@operator div(Vector3|number): Vector3
---@operator unm: Vector3
---@operator eq(Vector3): boolean
---@operator tostring: string
---@overload fun(): Vector3
---@overload fun(x: number, y: number, z: number): Vector3
Vector3 = {}

--- Create a zero-initialized value
---@overload fun(x: number, y: number, z: number): Vector3
---@return Vector3
function Vector3.new() end

--- Return a copy of this vector
---@return Vector3
function Vector3:Copy() end

--- Return the cross product with another Vector3
---@param other Vector3
---@return Vector3
function Vector3:Cross(other) end

--- Return the distance to another vector
---@param other Vector3
---@return number
function Vector3:Distance(other) end

--- Return the dot product with another vector
---@param other Vector3
---@return number
function Vector3:Dot(other) end

--- Return the vector magnitude
---@return number
function Vector3:Length() end

--- Return the squared vector magnitude
---@return number
function Vector3:LengthSqr() end

--- Linearly interpolate to another vector
---@param other Vector3
---@param amount number
---@return Vector3
function Vector3:Lerp(other, amount) end

--- Return a normalized copy; zero remains zero
---@return Vector3
function Vector3:Normalize() end

--- Vector4 value type. Supports component-wise arithmetic with other vectors or numbers.
---@class Vector4
---Ownership: value
---@field x number # X component
---@field y number # Y component
---@field z number # Z component
---@field w number # W component
---@operator add(Vector4|number): Vector4
---@operator sub(Vector4|number): Vector4
---@operator mul(Vector4|number): Vector4
---@operator div(Vector4|number): Vector4
---@operator unm: Vector4
---@operator eq(Vector4): boolean
---@operator tostring: string
---@overload fun(): Vector4
---@overload fun(x: number, y: number, z: number, w: number): Vector4
Vector4 = {}

--- Create a zero-initialized value
---@overload fun(x: number, y: number, z: number, w: number): Vector4
---@return Vector4
function Vector4.new() end

--- Return a copy of this vector
---@return Vector4
function Vector4:Copy() end

--- Return the distance to another vector
---@param other Vector4
---@return number
function Vector4:Distance(other) end

--- Return the dot product with another vector
---@param other Vector4
---@return number
function Vector4:Dot(other) end

--- Return the vector magnitude
---@return number
function Vector4:Length() end

--- Return the squared vector magnitude
---@return number
function Vector4:LengthSqr() end

--- Linearly interpolate to another vector
---@param other Vector4
---@param amount number
---@return Vector4
function Vector4:Lerp(other, amount) end

--- Return a normalized copy; zero remains zero
---@return Vector4
function Vector4:Normalize() end

--- Functions for finding, creating, cloning and destroying entities.
---@class Entities
Entities = {}

--- Clone an entity and its reflected component/script data
---@param ref EntityRef
---@return EntityRef|nil
function Entities.Clone(ref) end

--- Create an entity with its default name
---@overload fun(name: string): EntityRef
---@overload fun(name: string, tags: string|string[]): EntityRef
---@return EntityRef
function Entities.Create() end

--- Destroy the referenced entity
---@param ref EntityRef
---@return boolean
function Entities.Destroy(ref) end

--- Check whether an EntityRef currently resolves
---@param ref EntityRef
---@return boolean
function Entities.Exists(ref) end

--- Find the first entity with the specified name
---@param name string
---@return EntityRef|nil
function Entities.Find(name) end

--- Find all entities with the specified name
---@param name string
---@param options? table
---@return EntityRef[]
function Entities.FindAll(name, options) end

--- Find entities carrying every named component
---@param ... string
---@return EntityRef[]
function Entities.FindWithComponent(...) end

--- Find all entities carrying a tag
---@param tag string
---@param options? table
---@return EntityRef[]
function Entities.FindWithTag(tag, options) end

--- Resolve an entity UUID
---@param uuid string
---@return EntityRef|nil
function Entities.FromId(uuid) end

--- Named event listeners and event dispatch between scripts.
---@class Events
Events = {}

--- Immediately invoke listeners and return the number called.
---@param name string
---@param ... any
---@return integer
function Events.Emit(name, ...) end

--- Subscribe to an event. Returns a listener ID.
---@param name string
---@param callback function
---@return integer
function Events.Latch(name, callback) end

--- Subscribe to an event for one invocation only.
---@param name string
---@param callback function
---@return integer
function Events.LatchOnce(name, callback) end

--- Queue an event for delivery after the current script update.
---@param name string
---@param ... any
---@return nil
function Events.Post(name, ...) end

--- Remove a listener by its ID.
---@param id integer
---@return boolean
function Events.Release(id) end

--- The per-script context table passed to each script factory.
---@class ScriptContext
local ScriptContext = {}

--- Get a live native handle to a script-exposed component, or nil
---@generic T: ComponentHandle
---@param name `T`
---@return T|nil
function ScriptContext:GetComponent(name) end

--- Return the EntityRef belonging to this script instance
---@return EntityRef
function ScriptContext:GetEntity() end

--- Declare a persistent, inspector-visible script field
---@param name string
---@param value any
---@return any
function ScriptContext:expose(name, value) end

--- Keyboard and mouse input state for the current frame.
---@class input
input = {}

--- Get mouse movement accumulated for the current script step.
---@return MirageTable1
function input.GetMouseDelta() end

--- Get the mouse position in window pixels as an x/y/z table.
---@return MirageTable2
function input.GetMousePosition() end

--- Get the mouse-wheel movement for the current script step.
---@return number
function input.GetMouseWheel() end

--- Get the mouse position in world units as an x/y/z table.
---@return MirageTable2
function input.GetMouseWorldPosition() end

--- Get the mouse X coordinate in window pixels.
---@return number
function input.GetMouseX() end

--- Get the mouse Y coordinate in window pixels.
---@return number
function input.GetMouseY() end

--- Check whether a keyboard key is currently held down.
---@param key string
---@return boolean
function input.IsKeyDown(key) end

--- Check whether a keyboard key was pressed this frame.
---@param key string
---@return boolean
function input.IsKeyPressed(key) end

--- Check whether a keyboard key was released this frame.
---@param key string
---@return boolean
function input.IsKeyReleased(key) end

--- Check whether a keyboard key is currently up.
---@param key string
---@return boolean
function input.IsKeyUp(key) end

--- Check whether a mouse button is currently held down.
---@param button string|integer
---@return boolean
function input.IsMouseDown(button) end

--- Check whether a mouse button was pressed this frame.
---@param button string|integer
---@return boolean
function input.IsMousePressed(button) end

--- Check whether a mouse button was released this frame.
---@param button string|integer
---@return boolean
function input.IsMouseReleased(button) end

--- Check whether a mouse button is currently up.
---@param button string|integer
---@return boolean
function input.IsMouseUp(button) end

--- Get a live native handle to a script-exposed component, or nil
---@generic T: ComponentHandle
---@param name `T`
---@return T|nil
function GetComponent(name) end

--- Declare a persistent, inspector-visible script field
---@param name string
---@param value any
---@return any
function expose(name, value) end

--- Clamp a number between a minimum and maximum
---@param value number
---@param minimum number
---@param maximum number
---@return number
function math.clamp(value, minimum, maximum) end

--- Linearly interpolate between two numbers
---@param a number
---@param b number
---@param amount number
---@return number
function math.lerp(a, b, amount) end

--- Round to the nearest integer; halves round away from zero
---@param value number
---@return number
function math.round(value) end

--- Return -1, 0, or 1 based on the sign of a number
---@param value number
---@return number
function math.sign(value) end

--- Write tab-separated values to the Mirage script log.
---@param ... any
---@return nil
function print(...) end

