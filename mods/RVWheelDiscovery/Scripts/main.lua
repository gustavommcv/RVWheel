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

-- F10 schema discovery: distinct from F9's REFLECTION_KEYWORDS above --
-- F9 looks for input/gear-related members, F10 looks for physics/telemetry
-- ones (speed, lateral velocity, yaw rate, suspension, collisions, ...)
-- that F9's keyword list was never going to match. This is why the
-- feasibility research's open question ("does AVS expose speed/velocity")
-- was never actually answered: the prior probe could not have found it.
local TELEMETRY_KEYWORDS = {
    "speed", "veloc", "angular", "yaw", "lateral", "forward", "rpm", "slip",
    "suspension", "compression", "load", "contact", "surface", "impact",
    "collision", "movement", "component", "chassis", "wheel",
}

-- Narrower subset of TELEMETRY_KEYWORDS used only to decide whether an
-- ObjectProperty/ObjectPtrProperty is worth following as a component (one
-- level deep) -- see try_follow_component. Every one of these is already
-- part of TELEMETRY_KEYWORDS, so anything that would match this list has
-- necessarily already been logged as a relevant property in its own right.
local COMPONENT_FOLLOW_KEYWORDS = { "component", "movement", "wheel", "suspension", "chassis" }

-- Explicit global limits (F10 requirement): bounds both how deep the
-- pawn's own superclass chain is walked and how many matching pawn
-- members get logged, independently of the separate, smaller
-- component-following budget below.
local F10_MAX_DEPTH = 16
local F10_MAX_MEMBERS = 300

-- Separate, smaller budget for following ObjectProperty/ObjectPtrProperty
-- values as component objects -- a total across every component followed
-- during one F10 run, not per-component. Deliberately much smaller than
-- F10_MAX_MEMBERS: following components is the part of this feature that
-- touches live game objects rather than only class-level reflection data.
local F10_MAX_COMPONENT_OBJECTS = 16
local F10_MAX_COMPONENT_MEMBERS = 200

local CM_PER_S_TO_KM_PER_H = 0.036

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

local function name_matches_any(name, keywords)
    local lowered = string.lower(name)
    for _, keyword in ipairs(keywords) do
        if string.find(lowered, keyword, 1, true) ~= nil then
            return true
        end
    end
    return false
end

-- Wrapped in its own pcall (unlike F9's is_relevant_member) so one member
-- whose name can't be read for any reason is skipped, not fatal to the
-- whole ForEachFunction/ForEachProperty pass it's called from.
local function is_telemetry_member(member)
    local ok, name = pcall(function() return member:GetFName():ToString() end)
    if not ok then
        return false
    end
    return name_matches_any(name, TELEMETRY_KEYWORDS)
end

local function reflected_member_type(member)
    local ok, typeName = pcall(function() return member:GetClass():GetFName():ToString() end)
    if ok then
        return typeName
    end
    return "unknown"
end

-- Logs exactly what F10 requires per member: full name, reflected
-- type (FloatProperty/ObjectProperty/Function/...), and owning class.
-- Never invokes the member -- functions are only ever described, never
-- called, matching F9's own existing behavior.
local function log_schema_member(kind, member, ownerClassFullName)
    local ok, fullName = pcall(function() return member:GetFullName() end)
    if not ok then
        log(string.format("F10 %s: <unreadable full name> owner=%s", kind, ownerClassFullName))
        return
    end
    log(string.format("F10 %s: type=%s owner=%s full=%s", kind, reflected_member_type(member), ownerClassFullName, fullName))
end

-- Follows ONE ObjectProperty/ObjectPtrProperty value as a live component,
-- exactly one level deep (its own class's functions/properties only --
-- never that component's own object-typed members, and never its
-- superclasses). Deduplicated by GetAddress() and bounded by `budget` so
-- repeated properties pointing at the same component, or many components,
-- can never make this unbounded. Deliberately does not assume
-- vehicle:GetComponents() exists -- this only ever follows a component
-- because a reflected property on the pawn's own class chain named it.
local function try_follow_component(instance, property, budget)
    if budget.objects >= F10_MAX_COMPONENT_OBJECTS or budget.members >= F10_MAX_COMPONENT_MEMBERS then
        return
    end

    local nameOk, propertyName = pcall(function() return property:GetFName():ToString() end)
    if not nameOk then
        return
    end

    local readOk, component = pcall(function() return instance[propertyName] end)
    if not readOk or component == nil then
        return
    end

    local validOk, isValid = pcall(function() return component:IsValid() end)
    if not validOk or not isValid then
        return
    end

    local addressOk, address = pcall(function() return component:GetAddress() end)
    if not addressOk or address == nil then
        return
    end
    if budget.visited[address] then
        return
    end
    budget.visited[address] = true
    budget.objects = budget.objects + 1

    local fullNameOk, fullName = pcall(function() return component:GetFullName() end)
    log(string.format("F10 component: property=%s address=%s full=%s",
        propertyName, tostring(address), fullNameOk and fullName or "<unreadable>"))

    local classOk, componentClass = pcall(function() return component:GetClass() end)
    if not classOk or componentClass == nil then
        return
    end
    local classValidOk, classValid = pcall(function() return componentClass:IsValid() end)
    if not classValidOk or not classValid then
        return
    end
    local classNameOk, componentClassFullName = pcall(function() return componentClass:GetFullName() end)
    local ownerLabel = classNameOk and componentClassFullName or "<unknown class>"

    -- One level only: this class's own members, never its superclasses,
    -- and never recursing into whatever object-typed members it has.
    pcall(function()
        componentClass:ForEachFunction(function(func)
            if budget.members >= F10_MAX_COMPONENT_MEMBERS then
                return true
            end
            if is_telemetry_member(func) then
                log_schema_member("component-function", func, ownerLabel)
                budget.members = budget.members + 1
            end
        end)
    end)
    pcall(function()
        componentClass:ForEachProperty(function(prop)
            if budget.members >= F10_MAX_COMPONENT_MEMBERS then
                return true
            end
            if is_telemetry_member(prop) then
                log_schema_member("component-property", prop, ownerLabel)
                budget.members = budget.members + 1
            end
        end)
    end)
end

-- F10: targeted AVS telemetry schema discovery. Manual, once-per-keypress,
-- game-thread only (see the RegisterKeyBind below) -- never per-tick, and
-- never a global object scan (no ForEachUObject anywhere here). Walks the
-- possessed pawn's own class and superclasses only, exactly like F9's
-- capture_vehicle_reflection, but against TELEMETRY_KEYWORDS instead of
-- F9's input-focused list, and additionally follows a small, bounded set
-- of component object references one level deep.
local function capture_avs_schema()
    local vehicle = UEHelpers.GetPlayer()
    if vehicle == nil or not vehicle:IsValid() then
        log("F10 schema discovery skipped: PlayerPawn unavailable")
        return
    end

    log("F10 schema discovery begin: " .. vehicle:GetFullName())

    local componentBudget = { objects = 0, members = 0, visited = {} }
    local class = vehicle:GetClass()
    local depth = 0
    local emitted = 0

    while class ~= nil and class:IsValid() and depth < F10_MAX_DEPTH and emitted < F10_MAX_MEMBERS do
        local classNameOk, classFullName = pcall(function() return class:GetFullName() end)
        local ownerLabel = classNameOk and classFullName or "<unknown class>"
        log(string.format("F10 class[%d]: %s", depth, ownerLabel))

        pcall(function()
            class:ForEachFunction(function(func)
                if emitted >= F10_MAX_MEMBERS then
                    return true
                end
                if is_telemetry_member(func) then
                    log_schema_member("function", func, ownerLabel)
                    emitted = emitted + 1
                end
            end)
        end)

        if emitted < F10_MAX_MEMBERS then
            pcall(function()
                class:ForEachProperty(function(property)
                    if emitted >= F10_MAX_MEMBERS then
                        return true
                    end
                    if is_telemetry_member(property) then
                        log_schema_member("property", property, ownerLabel)
                        emitted = emitted + 1

                        local typeName = reflected_member_type(property)
                        if typeName == "ObjectProperty" or typeName == "ObjectPtrProperty" then
                            local followNameOk, propertyName = pcall(function() return property:GetFName():ToString() end)
                            if followNameOk and name_matches_any(propertyName, COMPONENT_FOLLOW_KEYWORDS) then
                                try_follow_component(vehicle, property, componentBudget)
                            end
                        end
                    end
                end)
            end)
        end

        local superOk, super = pcall(function() return class:GetSuperStruct() end)
        class = superOk and super or nil
        depth = depth + 1
    end

    if emitted >= F10_MAX_MEMBERS then
        log("F10: pawn-class member output limited to " .. F10_MAX_MEMBERS)
    end
    log(string.format(
        "F10 schema discovery end: %d pawn members, %d component objects, %d component members",
        emitted, componentBudget.objects, componentBudget.members))
end

local function vector_dot(a, b)
    return a.X * b.X + a.Y * b.Y + a.Z * b.Z
end

local function vector_magnitude(v)
    return math.sqrt(v.X * v.X + v.Y * v.Y + v.Z * v.Z)
end

-- F11: lightweight standard-Actor telemetry snapshot. No reflection at
-- all -- only four known, read-only AActor functions, each independently
-- pcall-protected so one being unavailable in this game/UE4SS version
-- logs "unavailable" and the rest still run. Never writes a property and
-- never calls anything that takes parameters.
local function capture_actor_telemetry_snapshot()
    local vehicle = UEHelpers.GetPlayer()
    if vehicle == nil or not vehicle:IsValid() then
        log("F11 telemetry snapshot skipped: PlayerPawn unavailable")
        return
    end

    log("F11 telemetry snapshot begin: " .. vehicle:GetFullName())

    local function callReadOnly(functionName)
        local ok, result = pcall(function() return vehicle[functionName](vehicle) end)
        if not ok then
            log(string.format("F11 %s: unavailable (%s)", functionName, tostring(result)))
            return nil
        end
        return result
    end

    local velocity = callReadOnly("GetVelocity")
    local forward = callReadOnly("GetActorForwardVector")
    local right = callReadOnly("GetActorRightVector")
    local rotation = callReadOnly("GetActorRotation")

    if rotation ~= nil then
        local ok, message = pcall(function()
            return string.format("F11 rotation: pitch=%.3f yaw=%.3f roll=%.3f", rotation.Pitch, rotation.Yaw, rotation.Roll)
        end)
        log(ok and message or ("F11 rotation: unreadable (" .. tostring(message) .. ")"))
    end

    if velocity == nil or forward == nil or right == nil then
        log("F11 telemetry snapshot end: insufficient vectors for speed/forwardSpeed/lateralSpeed")
        return
    end

    local ok, message = pcall(function()
        local speedCmPerS = vector_magnitude(velocity)
        return string.format(
            "F11 velocity: X=%.3f Y=%.3f Z=%.3f | speed=%.3f cm/s (%.3f km/h) | forwardSpeed=%.3f cm/s | lateralSpeed=%.3f cm/s",
            velocity.X, velocity.Y, velocity.Z,
            speedCmPerS, speedCmPerS * CM_PER_S_TO_KM_PER_H,
            vector_dot(velocity, forward), vector_dot(velocity, right)
        )
    end)
    log(ok and message or ("F11 telemetry math failed: " .. tostring(message)))

    log("F11 telemetry snapshot end")
end

local function capture_on_game_thread(reason)
    ExecuteInGameThread(function()
        local ok, error_message = pcall(capture_snapshot, reason)
        if not ok then
            log("snapshot failed: " .. tostring(error_message))
        end
    end)
end

log("loaded successfully; F8 captures objects, F9 captures vehicle input reflection, "
    .. "F10 captures a targeted AVS telemetry schema, F11 captures a lightweight Actor telemetry snapshot")
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

RegisterKeyBind(Key.F10, function()
    ExecuteInGameThread(function()
        local ok, error_message = pcall(capture_avs_schema)
        if not ok then
            log("F10 schema discovery failed: " .. tostring(error_message))
        end
    end)
end)

RegisterKeyBind(Key.F11, function()
    ExecuteInGameThread(function()
        local ok, error_message = pcall(capture_actor_telemetry_snapshot)
        if not ok then
            log("F11 telemetry snapshot failed: " .. tostring(error_message))
        end
    end)
end)

RegisterHook("/Script/Engine.PlayerController:ClientRestart", function()
    capture_on_game_thread("PlayerController.ClientRestart")
end)
