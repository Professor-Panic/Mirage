return function(entity)
    local transform=entity:GetComponent("Transform")
    expose("speed",5)
    expose("target",EntityRef())
    function Start()
        print("Script started!")
        
        print(transform.position.x)
        print(entity.target:HasComponent("Transform"))
    end
    function Update(dt)
        transform.position.x=transform.position.x+entity.speed*dt;
        print(input.IsKeyDown("KEY_A"));
    end
    return {
        Start = Start,
        Update = Update
    }
end