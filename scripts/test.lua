local function lerp(a, b, t)
    a = Vector3(a.x, a.y, a.z)
    b = Vector3(b.x, b.y, b.z) 
    return a + (b - a) * t
end
return function(entity)
    local transform=entity:GetComponent("Transform")
    local left=false;
    local mouse_pos;
    expose("speed",5)
    expose("target",EntityRef())
    function Start()
        print("Script started!")
        
        print(transform.position.x)
        print(entity.target:HasComponent("Transform"))
    end
    function Update(dt)
        if input.IsMouseDown("left") then
            mouse_pos = input.GetMouseWorldPosition()
        end
        if mouse_pos then
            transform.position = lerp(transform.position, mouse_pos, 0.5)
        end
    end
    return {
        Start = Start,
        Update = Update
    }
end
