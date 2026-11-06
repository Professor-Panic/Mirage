return function(self)
    expose("Speed", 5.0)
    expose("JumpSpeed", 10.0)
    local body;
    local sprite;
    local left;
    function Start()
        body = self:GetComponent("RigidBody2D")
        sprite=self:GetComponent("Sprite")
    end

    function Update(dt)
        local v = body.linearVelocity      -- keep y, so gravity and jumps still work
        if input.IsKeyDown("KEY_D") then
            v.x = self.Speed
            left=false
        elseif input.IsKeyDown("KEY_A") then
            v.x = -self.Speed
            left=true
        else
            v.x = 0
        end
        if input.IsKeyPressed("KEY_SPACE") then
            v.y=-self.JumpSpeed
        end
        if left then
            sprite.flipX=true
        else
            sprite.flipX=false
        end
        body.linearVelocity = v
    end

    return { Start = Start, Update = Update }
end