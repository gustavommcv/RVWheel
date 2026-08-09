local UEHelpers = require("UEHelpers")

local PREFIX = "[RVWheel]"
local STALE_SECONDS = 2
local localAppData = os.getenv("LOCALAPPDATA") or "."
local bridgePath = localAppData .. "\\RVWheel\\runtime\\bridge-state.txt"
local CLUTCH_SHIFT_THRESHOLD = 0.5

-- RVT1 vehicle telemetry transport (Lua -> native), independent of the
-- RVW2 bridge-state transport above (which carries native -> Lua input).
-- See tools/device_probe/VehicleTelemetryTransport.hpp for the C++ side
-- and docs/game-integration/AVS_TELEMETRY_DISCOVERY.md for why only
-- GetVelocity/GetActorForwardVector/GetActorRightVector are used here --
-- these are the only vehicle telemetry calls confirmed working in this
-- game; nothing here uses reflection.
local telemetryPath = localAppData .. "\\RVWheel\\runtime\\vehicle-telemetry.txt"
local TELEMETRY_PUBLISH_INTERVAL_SECONDS = 1.0 / 20.0 -- 20 Hz cap.
local CM_PER_S_TO_M_PER_S = 0.01
local telemetrySequence = 0
local lastTelemetryPublishClock = nil
local telemetryErrorLogged = false
local SHIFT_REVERSE = 1
local SHIFT_NEUTRAL = 2
local SHIFT_DRIVE = 3

-- Device-specific H-pattern layouts belong at this boundary, while RVW2
-- transports all 128 raw buttons generically. Add verified VID:PID maps here
-- without changing the wheel DAL or the vehicle input logic.
local H_PATTERN_SHIFTERS = {
    ["046D:C266"] = {
        [12] = 1,
        [13] = 2,
        [14] = 3,
        [15] = 4,
        [16] = 5,
        [17] = 6,
        [18] = -1,
    },
}

local lastSequence = nil
local lastFreshClock = nil
local lastGoodState = nil
local bridgeWasActive = false
local setterErrorLogged = false
local tickHookInstalled = false
local lastSteeringDiagnosticSecond = nil
local lastButtonSignature = nil
local lastAppliedGear = nil
local shifterErrorLogged = false

local function log(message)
    print(string.format("%s %s\n", PREFIX, message))
end

local function clamp(value, minimum, maximum)
    if value == nil or value ~= value or value == math.huge or value == -math.huge then
        return nil
    end
    if value < minimum then return minimum end
    if value > maximum then return maximum end
    return value
end

local function readBridgeState()
    local function cachedStateIfFresh()
        if lastGoodState ~= nil and lastFreshClock ~= nil and os.time() - lastFreshClock <= STALE_SECONDS then
            return lastGoodState
        end
        return nil
    end

    local file = io.open(bridgePath, "r")
    if file == nil then
        return cachedStateIfFresh()
    end

    local line = file:read("*l")
    file:close()
    if line == nil then
        return cachedStateIfFresh()
    end

    local seqStart, connected, valid, steeringText, throttleText, brakeText, clutchText,
        vendorText, productText, buttons0Text, buttons1Text, buttons2Text, buttons3Text, seqEnd =
        line:match("^RVW2%s+(%d+)%s+(%d)%s+(%d)%s+(%S+)%s+(%S+)%s+(%S+)%s+(%S+)%s+([0-9A-Fa-f]+)%s+([0-9A-Fa-f]+)%s+([0-9A-Fa-f]+)%s+([0-9A-Fa-f]+)%s+([0-9A-Fa-f]+)%s+([0-9A-Fa-f]+)%s+(%d+)%s*$")
    if seqStart == nil or seqStart ~= seqEnd then
        return cachedStateIfFresh()
    end

    local sequence = tonumber(seqStart)
    if sequence ~= lastSequence then
        lastSequence = sequence
        lastFreshClock = os.time()
    end

    local steering = clamp(tonumber(steeringText), -1.0, 1.0)
    local throttle = clamp(tonumber(throttleText), 0.0, 1.0)
    local brake = clamp(tonumber(brakeText), 0.0, 1.0)
    local clutch = clamp(tonumber(clutchText), 0.0, 1.0)
    local buttonWords = {
        tonumber(buttons0Text, 16),
        tonumber(buttons1Text, 16),
        tonumber(buttons2Text, 16),
        tonumber(buttons3Text, 16),
    }
    if steering == nil or throttle == nil or brake == nil or clutch == nil or
        buttonWords[1] == nil or buttonWords[2] == nil or buttonWords[3] == nil or buttonWords[4] == nil then
        return nil
    end

    if connected ~= "1" or valid ~= "1" or lastFreshClock == nil or os.time() - lastFreshClock > STALE_SECONDS then
        lastGoodState = nil
        return nil
    end

    lastGoodState = {
        sequence = sequence,
        steering = steering,
        throttle = throttle,
        brake = brake,
        clutch = clutch,
        deviceKey = string.upper(vendorText .. ":" .. productText),
        buttonWords = buttonWords,
    }
    return lastGoodState
end

local function isButtonPressed(buttonWords, buttonIndex)
    local wordIndex = math.floor(buttonIndex / 32) + 1
    local bitIndex = buttonIndex % 32
    return math.floor(buttonWords[wordIndex] / (2 ^ bitIndex)) % 2 == 1
end

local function resolveManualGear(state)
    local mapping = H_PATTERN_SHIFTERS[state.deviceKey]
    if mapping == nil then return nil end

    local selected = 0 -- No mapped gate is the physical neutral position.
    for buttonIndex, gear in pairs(mapping) do
        if isButtonPressed(state.buttonWords, buttonIndex) then
            if selected ~= 0 then
                return nil -- Reject an electrically ambiguous H-pattern state.
            end
            selected = gear
        end
    end
    return selected
end

local function applyShifter(vehicle, state)
    local desiredGear = resolveManualGear(state)
    if desiredGear == nil or desiredGear == lastAppliedGear then return end
    if state.clutch < CLUTCH_SHIFT_THRESHOLD then return end

    local ok, errorMessage = pcall(function()
        local gearBox = vehicle.GearBox
        local function setShifterPosition(position)
            local rpcOk = pcall(function()
                vehicle:RPC_Server_Shifter(position)
            end)
            if not rpcOk then
                vehicle:SetShifterPosition(position)
            end
        end
        local function updateGearBox(displayGear)
            if gearBox ~= nil and gearBox:IsValid() then
                gearBox:Server_SetCurrentGear(displayGear)
            end
        end

        -- Match the game's own GearBox flow used by the established
        -- GearHotkeys UE4SS mod. SetManualGear alone changes only the AVS
        -- transmission and leaves the Ride shifter/HUD state inconsistent.
        if desiredGear < 0 then
            setShifterPosition(SHIFT_DRIVE)
            vehicle:SetManualGear(1)
            setShifterPosition(SHIFT_REVERSE)
            updateGearBox(-1)
        elseif desiredGear == 0 or desiredGear > 5 then
            setShifterPosition(SHIFT_NEUTRAL)
            updateGearBox(0)
        else
            setShifterPosition(SHIFT_DRIVE)
            vehicle:SetManualGear(desiredGear)
            updateGearBox(desiredGear)
        end

        log("manual gear requested=" .. tostring(desiredGear) ..
            " CurrentGear=" .. tostring(vehicle.CurrentGear) ..
            " GearInput=" .. tostring(vehicle.GearInput) ..
            " clutch=" .. tostring(state.clutch))
    end)
    if ok then
        lastAppliedGear = desiredGear
        shifterErrorLogged = false
    elseif not shifterErrorLogged then
        shifterErrorLogged = true
        log("manual gear setter failed: " .. tostring(errorMessage))
    end
end

local function describePressedButtons(buttonWords)
    local pressed = {}
    for wordIndex = 1, 4 do
        local word = buttonWords[wordIndex]
        for bitIndex = 0, 31 do
            if math.floor(word / (2 ^ bitIndex)) % 2 == 1 then
                table.insert(pressed, tostring((wordIndex - 1) * 32 + bitIndex))
            end
        end
    end
    if #pressed == 0 then return "(none)" end
    return table.concat(pressed, ",")
end

local function logButtonChanges(vehicle, state)
    local signature = table.concat(state.buttonWords, ":")
    if signature ~= lastButtonSignature then
        lastButtonSignature = signature
        log("buttons pressed: " .. describePressedButtons(state.buttonWords))
        local gearOk, gearMessage = pcall(function()
            local gearsCount = "?"
            if vehicle.Gears ~= nil then
                gearsCount = tostring(vehicle.Gears:GetArrayNum())
            end
            return "gear state: CurrentGear=" .. tostring(vehicle.CurrentGear) ..
                " GearInput=" .. tostring(vehicle.GearInput) ..
                " Gears=" .. gearsCount
        end)
        if gearOk then
            log(gearMessage)
        else
            log("gear state diagnostic failed: " .. tostring(gearMessage))
        end
    end
end

local function applyInputs(vehicle, state)
    local ok, errorMessage = pcall(function()
        vehicle:SetSteeringInput(state.steering)
        -- TickInputs is a Blueprint function, and UE4SS invokes this hook
        -- after it returns. SetSteeringInput updates SteeringInput and
        -- LocalSteeringInput correctly, but CalculateSteering has already
        -- run for this frame. Publish the same bounded value to the final
        -- Steering property consumed by the later vehicle/physics work.
        vehicle:SetThrottleInput(state.throttle)
        vehicle:SetBrakeInput(state.brake)
        -- Keep this last: the throttle/brake setters may run UpdateInputs
        -- and recalculate Steering from the game's native input path.
        vehicle.Steering = state.steering
    end)

    if not ok and not setterErrorLogged then
        setterErrorLogged = true
        log("vehicle input setter failed: " .. tostring(errorMessage))
    end


    local diagnosticSecond = os.time()
    if ok and math.abs(state.steering) >= 0.2 and diagnosticSecond ~= lastSteeringDiagnosticSecond then
        lastSteeringDiagnosticSecond = diagnosticSecond
        local diagnosticOk, diagnosticMessage = pcall(function()
            return "steering requested=" .. tostring(state.steering) ..
                " input=" .. tostring(vehicle.SteeringInput) ..
                " local=" .. tostring(vehicle.LocalSteeringInput) ..
                " calculated=" .. tostring(vehicle.Steering)
        end)
        if diagnosticOk then
            log(diagnosticMessage)
        else
            log("steering diagnostic failed: " .. tostring(diagnosticMessage))
        end
    end
    return ok
end

local function vehicleTelemetryDot(a, b)
    return a.X * b.X + a.Y * b.Y + a.Z * b.Z
end

-- Publishes one RVT1 line, at most once every
-- TELEMETRY_PUBLISH_INTERVAL_SECONDS, for the currently possessed local
-- vehicle. Deliberately independent of readBridgeState()/applyInputs()
-- below: this must keep publishing whether or not the input bridge is
-- active, and must never touch input, gear, or force feedback state.
-- `valid`/`local` are always written as 1 here because this function is
-- only ever called from onVehicleTick after it has already confirmed
-- `vehicle` is the possessed local pawn -- if the player leaves the
-- vehicle, this function simply stops being called (the hook's own
-- address check below fails first), so the file's last-written sample
-- ages in place; a consumer is expected to treat that as staleness
-- itself, not to expect an explicit "invalid" frame for that case.
local function publishVehicleTelemetry(vehicle)
    local now = os.clock()
    if lastTelemetryPublishClock ~= nil and (now - lastTelemetryPublishClock) < TELEMETRY_PUBLISH_INTERVAL_SECONDS then
        return
    end
    lastTelemetryPublishClock = now

    local ok, errorMessage = pcall(function()
        local velocity = vehicle:GetVelocity()
        local forward = vehicle:GetActorForwardVector()
        local right = vehicle:GetActorRightVector()

        local speedCmPerS = math.sqrt(velocity.X * velocity.X + velocity.Y * velocity.Y + velocity.Z * velocity.Z)
        local forwardCmPerS = vehicleTelemetryDot(velocity, forward)
        local lateralCmPerS = vehicleTelemetryDot(velocity, right)

        telemetrySequence = telemetrySequence + 1
        local line = string.format(
            "RVT1 %d 1 1 %.4f %.4f %.4f - %d",
            telemetrySequence,
            speedCmPerS * CM_PER_S_TO_M_PER_S,
            forwardCmPerS * CM_PER_S_TO_M_PER_S,
            lateralCmPerS * CM_PER_S_TO_M_PER_S,
            telemetrySequence
        )

        local file = io.open(telemetryPath, "w")
        if file == nil then
            error("could not open telemetry file for writing: " .. telemetryPath)
        end
        file:write(line)
        file:close()
    end)

    if ok then
        telemetryErrorLogged = false
    elseif not telemetryErrorLogged then
        telemetryErrorLogged = true
        log("vehicle telemetry publish failed: " .. tostring(errorMessage))
    end
end

local function onVehicleTick(context)
    local vehicle = context:get()
    local playerPawn = UEHelpers.GetPlayer()
    if vehicle == nil or not vehicle:IsValid() or playerPawn == nil or not playerPawn:IsValid() or
        vehicle:GetAddress() ~= playerPawn:GetAddress() then
        return
    end

    -- Independent of the bridge-input state below: telemetry publishes
    -- every tick (rate-limited internally) regardless of whether the
    -- input bridge is active.
    publishVehicleTelemetry(vehicle)

    local state = readBridgeState()
    if state ~= nil then
        logButtonChanges(vehicle, state)
        applyShifter(vehicle, state)
        if applyInputs(vehicle, state) then
            if not bridgeWasActive then
                log("bridge active; applying wheel input to " .. vehicle:GetFullName())
            end
            bridgeWasActive = true
        end
    elseif bridgeWasActive then
        -- One neutral frame releases any last bridge value. Subsequent
        -- ticks are left entirely to the game's normal keyboard/gamepad
        -- input path instead of permanently overriding it with zeroes.
        applyInputs(vehicle, {steering = 0.0, throttle = 0.0, brake = 0.0})
        lastAppliedGear = nil
        bridgeWasActive = false
        log("bridge inactive or stale; returned control to the game")
    end
end

local function installVehicleTickHook()
    if tickHookInstalled then return end

    local ok, errorMessage = pcall(function()
        RegisterHook("/VehicleSystemPlugin/AVS_Vehicle.AVS_Vehicle_C:TickInputs", onVehicleTick)
    end)
    if ok then
        tickHookInstalled = true
        log("AVS vehicle input hook installed; waiting for a Ready bridge frame")
    else
        log("AVS vehicle class not ready yet; will retry after possession: " .. tostring(errorMessage))
    end
end

log("loaded; bridge state path: " .. bridgePath)
ExecuteInGameThread(installVehicleTickHook)

RegisterHook("/Script/Engine.PlayerController:ClientRestart", function()
    ExecuteInGameThread(installVehicleTickHook)
end)
