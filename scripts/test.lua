function Lerp(a,b,t)
   return (1-t)*a+t*b 
end
return function(entity)
    local transform=entity:GetComponent("Transform")
    local left=false;
    expose("speed",5)
    expose("target",EntityRef())
    function Start()
        print("Script started!")
        
        print(transform.position.x)
        print(entity.target:HasComponent("Transform"))
    end
    function Update(dt)
        if (input.IsKeyDown("KEY_A")) then
             transform.position.x=transform.position.x-entity.speed*dt;
        end
        if (input.IsKeyDown("KEY_D")) then
             transform.position.x=transform.position.x+entity.speed*dt;
        end
        if (input.IsKeyDown("KEY_W")) then
             transform.position.y=transform.position.y-entity.speed*dt;
        end
        if (input.IsKeyDown("KEY_S")) then
             transform.position.y=transform.position.y+entity.speed*dt;
        end
    end
    return {
        Start = Start,
        Update = Update
    }
end
