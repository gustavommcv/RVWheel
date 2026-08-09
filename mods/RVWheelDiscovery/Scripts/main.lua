local UEHelpers = require("UEHelpers")

local PREFIX = "[RVWheelDiscovery]"
local REFLECTION_KEYWORDS = {
    "input",
    "axis",
    "steer",
    "throttle",
    "acceler",
    "brake",
    "handbrake",
    "gas",
    "gear",
    "reverse",
    "drive",
    "vehicle",
    "wheel",
    "move",
}

local GEAR_FUNCTIONS = {
    SetGearItem = true,
    SetManualGear = true,
    ChangeGear = true,
    MoveShifterPosition = true,
}

local function log(message)
    print(string.format("%s %s\n", PREFIX, message))
end

local function describe_object(label, object)
    if object == nil or not object:IsValid() then
        log(label .. ": unavailable")
        return
    end

    log(string.format("%s: %s", label, object:GetFullName()))

    local class = object:GetClass()
    if class ~= nil and class:IsValid() then
        log(string.format("%s class: %s", label, class:GetFullName()))
    end
end

local function describe_instances(class_name)
    local ok, instances = pcall(FindAllOf, class_name)
    if not ok or instances == nil then
        return
    end

    log(string.format("%s instances: %d", class_name, #instances))
    for index, instance in ipairs(instances) do
        if index > 20 then
            log(string.format("%s: output limited to 20 instances", class_name))
            break
        end

        if instance ~= nil and instance:IsValid() then
            log(string.format("%s[%d]: %s", class_name, index, instance:GetFullName()))
        end
    end
end

local function capture_snapshot(reason)
    log("snapshot begin (" .. reason .. ")")

    describe_object("World", UEHelpers.GetWorld())
    describe_object("PlayerController", UEHelpers.GetPlayerController())
    describe_object("PlayerPawn", UEHelpers.GetPlayer())

    describe_instances("Pawn")
    describe_instances("Vehicle")
    describe_instances("WheeledVehiclePawn")
    describe_instances("ChaosWheeledVehiclePawn")
    describe_instances("WheeledVehicleMovementComponent")
    describe_instances("ChaosWheeledVehicleMovementComponent")

    log("snapshot end")
end

local function is_relevant_member(member)
    local full_name = string.lower(member:GetFName():ToString())
    for _, keyword in ipairs(REFLECTION_KEYWORDS) do
        if string.find(full_name, keyword, 1, true) ~= nil then
            return true
        end
    end
    return false
end

local function capture_vehicle_reflection()
    local vehicle = UEHelpers.GetPlayer()
    if vehicle == nil or not vehicle:IsValid() then
        log("reflection skipped: PlayerPawn unavailable")
        return
    end

    log("vehicle reflection begin: " .. vehicle:GetFullName())

    local class = vehicle:GetClass()
    local depth = 0
    local emitted = 0
    while class ~= nil and class:IsValid() and depth < 16 and emitted < 300 do
        log(string.format("reflection class[%d]: %s", depth, class:GetFullName()))

        class:ForEachFunction(function(func)
            if is_relevant_member(func) then
                emitted = emitted + 1
                log("function: " .. func:GetFullName())
                local functionName = func:GetFName():ToString()
                if GEAR_FUNCTIONS[functionName] then
                    local parameterCount = 0
                    func:ForEachProperty(function(property)
                        parameterCount = parameterCount + 1
                        log(string.format(
                            "gear parameter: function=%s index=%d type=%s name=%s full=%s",
                            functionName,
                            parameterCount,
                            property:GetClass():GetFName():ToString(),
                            property:GetFName():ToString(),
                            property:GetFullName()
                        ))
                    end)
                    log(string.format("gear signature: function=%s parameters=%d", functionName, parameterCount))
                end
            end
            if emitted >= 300 then
                return true
            end
        end)

        if emitted >= 300 then
            break
        end

        class:ForEachProperty(function(property)
            if is_relevant_member(property) then
                emitted = emitted + 1
                log("property: " .. property:GetFullName())
                if property:GetFName():ToString() == "Gears" and property:IsA(PropertyTypes.ArrayProperty) then
                    local inner = property:GetInner()
                    if inner ~= nil and inner:IsA(PropertyTypes.StructProperty) then
                        local gearStruct = inner:GetStruct()
                        log("gear array struct: " .. gearStruct:GetFullName())
                        gearStruct:ForEachProperty(function(field)
                            log(string.format(
                                "gear array field: type=%s name=%s full=%s",
                                field:GetClass():GetFName():ToString(),
                                field:GetFName():ToString(),
                                field:GetFullName()
                            ))
                        end)
                    end
                end
            end
            if emitted >= 300 then
                return true
            end
        end)

        class = class:GetSuperStruct()
        depth = depth + 1
    end

    if emitted >= 300 then
        log("reflection output limited to 300 matching members")
    end
    log(string.format("vehicle reflection end: %d matching members", emitted))
end

local function capture_on_game_thread(reason)
    ExecuteInGameThread(function()
        local ok, error_message = pcall(capture_snapshot, reason)
        if not ok then
            log("snapshot failed: " .. tostring(error_message))
        end
    end)
end

log("loaded successfully; F8 captures objects, F9 captures vehicle input reflection")
capture_on_game_thread("mod loaded")

RegisterKeyBind(Key.F8, function()
    capture_on_game_thread("F8")
end)

RegisterKeyBind(Key.F9, function()
    ExecuteInGameThread(function()
        local ok, error_message = pcall(capture_vehicle_reflection)
        if not ok then
            log("reflection failed: " .. tostring(error_message))
        end
    end)
end)

RegisterHook("/Script/Engine.PlayerController:ClientRestart", function()
    capture_on_game_thread("PlayerController.ClientRestart")
end)
