-- Boids, same rules and update order as before, written so the hot loop never
-- touches the engine. All simulation state lives in plain Lua arrays; the
-- transforms are only WRITTEN, once per boid per frame.

local WORLD_W = 25
local WORLD_H = 14.0625
local SPRITE_W = 0.2     -- sprite scale; also the wrap margin
local SPRITE_H = 0.5

local sqrt, atan, cos, sin = math.sqrt, math.atan, math.cos, math.sin
local rad, deg, pi = math.rad, math.deg, math.pi
local TWO_PI = 2 * pi

return function(self)
    expose("NumBoids", 50)
    expose("BoidVelocity", 10)
    expose("ConeAngle", 270)         -- full vision cone in degrees
    expose("SightRadius", 2.5)       -- how far a boid sees, in world units
    expose("TurnSpeed", 180)         -- max turn rate in degrees per second
    expose("SeparationWeight", 1.5)
    expose("AlignmentWeight", 0.5)
    expose("CohesionWeight", 1.0)
    local transforms = {}            -- engine handles: written once per frame, never read
    local px, py = {}, {}            -- position
    local ang = {}                   -- heading angle in radians (speed is constant)
    local hx, hy = {}, {}            -- unit heading, cos/sin of ang

    function Start()
        for i = 1, self.NumBoids do
            local ref = Entities.Create("boid", "boid")
            local transform = ref:GetComponent("Transform")
            transform.scale = Vector3(SPRITE_W, SPRITE_H, 0.0)
            ref:AddComponent("Sprite")
            local sprite = ref:GetComponent("Sprite")
            sprite.path = "textures/triangle.png"
            sprite.tint = Color(0,math.random()*55+200,math.random()*55+200)

            local x = math.random() * WORLD_W
            local y = math.random() * WORLD_H
            local a = math.random() * 2 * pi

            transforms[i] = transform
            px[i], py[i] = x, y
            ang[i] = a
            hx[i], hy[i] = cos(a), sin(a)
            transform.position = Vector3(x, y, 0)
            transform.eulerAngles = Vector3(0, 0, deg(a) + 90)
        end
    end

    function Update(dt)
        local n = #transforms

        -- Everything the inner loop needs, read once per frame instead of once per pair.
        local radius = self.SightRadius
        local radiusSq = radius * radius
        local cosHalfFov = cos(rad(self.ConeAngle * 0.5))
        local wSep = self.SeparationWeight
        local wAli = self.AlignmentWeight
        local wCoh = self.CohesionWeight
        local step = self.BoidVelocity * dt
        local maxTurn = rad(self.TurnSpeed) * dt

        for i = 1, n do
            local x, y = px[i], py[i]
            local fx, fy = hx[i], hy[i]

            local sepx, sepy = 0, 0
            local alix, aliy = 0, 0
            local cx, cy = 0, 0
            local count = 0

            for j = 1, n do
                if j ~= i then
                    local dx, dy = px[j] - x, py[j] - y
                    local distSq = dx * dx + dy * dy
                    -- cheapest test first: most pairs are out of range and stop here
                    if distSq > 0 and distSq < radiusSq then
                        local dist = sqrt(distSq)
                        -- Field of view without normalising:
                        -- dot(forward, offset) / dist >= cosHalf  <=>  dot >= cosHalf * dist
                        if fx * dx + fy * dy >= cosHalfFov * dist then
                            count = count + 1
                            -- separation: push away, stronger when closer.
                            -- (dx/dist) * (1 - dist/radius); dist < radius here so no clamp.
                            local w = (1 - dist / radius) / dist
                            sepx = sepx - dx * w
                            sepy = sepy - dy * w
                            alix = alix + hx[j]
                            aliy = aliy + hy[j]
                            cx = cx + px[j]
                            cy = cy + py[j]
                        end
                    end
                end
            end

            if count > 0 then
                -- separation: cap its length at 1 so close neighbours can push harder
                local sl = sepx * sepx + sepy * sepy
                if sl > 1 then
                    local inv = 1 / sqrt(sl)
                    sepx, sepy = sepx * inv, sepy * inv
                end

                -- alignment: direction of the summed neighbour headings
                local al = alix * alix + aliy * aliy
                if al > 0 then
                    local inv = 1 / sqrt(al)
                    alix, aliy = alix * inv, aliy * inv
                end

                -- cohesion: direction toward the neighbour centroid
                local ox, oy = cx / count - x, cy / count - y
                local ol = ox * ox + oy * oy
                if ol > 0 then
                    local inv = 1 / sqrt(ol)
                    ox, oy = ox * inv, oy * inv
                end

                local dsx = sepx * wSep + alix * wAli + ox * wCoh
                local dsy = sepy * wSep + aliy * wAli + oy * wCoh

                if dsx * dsx + dsy * dsy > 0 then
                    -- turn toward the desired heading by at most maxTurn
                    local diff = (atan(dsy, dsx) - ang[i] + pi) % TWO_PI - pi
                    if diff > maxTurn then diff = maxTurn
                    elseif diff < -maxTurn then diff = -maxTurn end
                    local a = ang[i] + diff
                    ang[i] = a
                    fx, fy = cos(a), sin(a)
                    hx[i], hy[i] = fx, fy
                end
            end
            -- integrate and wrap, in plain numbers
            x = x + fx * step
            y = y + fy * step
            if x > WORLD_W + SPRITE_W then x = -SPRITE_W
            elseif x < -SPRITE_W then x = WORLD_W + SPRITE_W end
            if y > WORLD_H + SPRITE_H then y = -SPRITE_H
            elseif y < -SPRITE_H then y = WORLD_H + SPRITE_H end
            px[i], py[i] = x, y

            -- the only engine access in the loop: two writes per boid
            local t = transforms[i]
            t.position = Vector3(x, y, 0)
            t.eulerAngles = Vector3(0, 0, deg(ang[i]) + 90)
        end
    end

    return {
        Start = Start,
        Update = Update
    }
end