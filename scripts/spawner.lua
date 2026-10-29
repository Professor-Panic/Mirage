function CreatePaddle(name,position)
    local e = Entities.Create(name)          -- has Transform + Game(name); returns an EntityRef
    e:AddComponent("Sprite")                     -- true if added
    e:GetComponent("Sprite").path = "textures/square.png"
    e:GetComponent("Sprite").tint = Color(255, 255, 255)
    e:GetComponent("Transform").position=position;
    e:GetComponent("Transform").scale=Vector3(1,6,0);
end
return function (Entity)
    expose("Test",0)
    function Start()
        CreatePaddle("Paddle_1",Vector3(50,400,0))
        CreatePaddle("Paddle_2",Vector3(1150,400,0))
    end
    return {
        Start=Start
    }
end