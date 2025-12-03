-- EVOFaderWing v0.3 Lua script for syncing EVOFaderWing using OSC
-- Sends ALL data as ONE OSC message: page + fader values (0-100) + color values
-- OSC sent when data has changed or after autoResendInterval
--
-- Default autoResendInterval is 300 (15 seconds)
--
-- Special thanks to xxpasixx for his pam-osc code which I modified for my project
-- GPL3


local function StartGui()
    -- Check current status
    local isRunning = GetVar(GlobalVars(), "opdateOSC") == true
    
    local descTable = {
        title = "EvoFaderWing OSC Control",
        caller = GetFocusDisplay(),
        add_args = {FilterSupport = "Yes"},
    }
    
    if isRunning then
        descTable.items = {"Stop", "Force Reload"}
        descTable.message = "EvoFaderWing OSC is RUNNING"
    else
        descTable.items = {"Start", "Force Reload"}  
        descTable.message = "EvoFaderWing OSC is STOPPED"
    end

    local index, name = PopupInput(descTable)
    return index, name
end

function main()
    local index, name = StartGui()

    if name == "Start" then
        Printf("Starting EvoFaderWing OSC...")
        -- Simply set the variable to true and let the original script run
        SetVar(GlobalVars(), "opdateOSC", true)
        
        local executorsToWatch = {}
        local oldValues = {}
        local oldColorValues = {}
        local oldExecStatus = {}

        local oscEntry = 2

        -- Keep the watch list ordered the same way we send status over OSC
        local execRanges = {
            {101, 110},
            {201, 210},
            {301, 310},
            {401, 410},
        }

        for _, range in ipairs(execRanges) do
            for i = range[1], range[2] do
                executorsToWatch[#executorsToWatch + 1] = i
                oldExecStatus[i] = 0
                oldColorValues[i] = "0,0,0,0"
            end
        end

        -- Set the default values for faders 201-210
        for i = 201, 210 do
            oldValues[i] = "000"
        end

        -- Set the default color values for both ranges (201-210 primary, 101-110 secondary)
        for i = 201, 210 do
            oldColorValues[i] = "0,0,0,0"
        end
        for i = 101, 110 do
            oldColorValues[i] = "0,0,0,0"
        end

        -- The speed to check executors
        local tick = 1 / 20 -- 1/20 second = 50ms
        local resendTick = 0
        local autoResendInterval = 300 -- ticks (15 seconds at 50ms per tick)
        local execStatusTypeTag = "," .. string.rep("i", 1 + #executorsToWatch) -- page + 40 status ints

        local function getAppearanceColor(sequence)
            local apper = sequence["APPEARANCE"]
            if apper ~= nil then
                return apper['BACKR'] .. "," .. apper['BACKG'] .. "," .. apper['BACKB'] .. "," .. apper['BACKALPHA']
            else
                return "255,255,255,255"
            end
        end

        local function readExecutorState(execNo, wantsValue, wantsColor)
            -- Pull executor info directly (avoids scanning the whole page every tick)
            local execHandle = GetExecutor(execNo)
            if execHandle == nil then
                return 0, "0,0,0,0", 0
            end

            local faderValue = 0
            if wantsValue then
                faderValue = execHandle:GetFader({token = "FaderMaster", faderDisabled = false}) or 0
            end

            local colorValue = "0,0,0,0"
            local isOn = false
            local isPopulated = execHandle.Object ~= nil

            local obj = execHandle.Object
            if obj ~= nil then
                if wantsColor then
                    colorValue = getAppearanceColor(obj)
                end

                local ok, activePlayback = pcall(function() return obj:HasActivePlayback() end)
                if ok and activePlayback ~= nil then
                    isOn = activePlayback == true or activePlayback == 1
                end

                -- Fallbacks in case HasActivePlayback is not available
                if not isOn then
                    if obj['isOn'] ~= nil then
                        isOn = obj['isOn'] ~= 0
                    elseif obj['IsOn'] ~= nil then
                        isOn = obj['IsOn'] ~= 0
                    elseif obj['RUNNING'] ~= nil then
                        isOn = obj['RUNNING'] ~= 0
                    elseif obj['Running'] ~= nil then
                        isOn = obj['Running'] ~= 0
                    end
                end
            end

            local statusCode = 0 -- 0=not populated, 1=populated/off, 2=populated/on
            if isPopulated then
                statusCode = isOn and 2 or 1
            end

            return faderValue, colorValue, statusCode
        end

        Printf("start EvoFaderWing OSC - fader values/colors + executor status (101-410)")
        Printf("autoResendInterval: " .. autoResendInterval .. " (every " .. (autoResendInterval / 20) .. " seconds)")

        local destPage = 1
        local forceReload = true

        while (GetVar(GlobalVars(), "opdateOSC")) do
            if GetVar(GlobalVars(), "forceReload") == true then
                forceReload = true
                SetVar(GlobalVars(), "forceReload", false)
            end

            -- Increment resend counter and check for automatic resend 
            resendTick = resendTick + 1
            if resendTick >= autoResendInterval then
                forceReload = true
                resendTick = 0
                Printf("Auto force reload triggered (every " .. (autoResendInterval / 20) .. " seconds)")
            end

            local faderDataChanged = false
            local statusChanged = false

            -- Check Page
            local myPage = CurrentExecPage()
            if myPage.index ~= destPage then
                destPage = myPage.index
                -- Reset fader values (201-210)
                for i = 201, 210 do
                    oldValues[i] = 000
                end
                -- Reset color values for both ranges (201-210 primary, 101-110 secondary)
                for i = 201, 210 do
                    oldColorValues[i] = "0,0,0,0"
                end
                for i = 101, 110 do
                    oldColorValues[i] = "0,0,0,0"
                end
                for _, execNo in ipairs(executorsToWatch) do
                    oldExecStatus[execNo] = 0
                end
                forceReload = true
                faderDataChanged = true
                statusChanged = true
            end

            -- Collect all current values
            local currentFaderValues = {}
            local currentColorValues = {}
            local currentExecStatus = {}

            for _, execNo in ipairs(executorsToWatch) do
                local wantsValue = execNo >= 201 and execNo <= 210
                local wantsColor = (execNo >= 101 and execNo <= 110) or (execNo >= 201 and execNo <= 210)

                local faderValue, colorValue, statusCode = readExecutorState(execNo, wantsValue, wantsColor)

                if wantsValue then
                    currentFaderValues[execNo] = faderValue
                    if oldValues[execNo] ~= faderValue then
                        faderDataChanged = true
                    end
                end

                if wantsColor then
                    currentColorValues[execNo] = colorValue
                    if oldColorValues[execNo] ~= colorValue then
                        faderDataChanged = true
                    end
                end

                currentExecStatus[execNo] = statusCode
                if oldExecStatus[execNo] ~= statusCode then
                    statusChanged = true
                end
            end

            -- Send bundled fader + color data
            if faderDataChanged or forceReload then
                -- Build OSC message: page + 10 fader values (0-100) + 10 dual color strings
                local oscMessage = "/faderUpdate,iiiiiiiiiiissssssssss," .. destPage
                
                -- Add all fader values (201-210) as 0-100 integers (no conversion needed)
                for i = 201, 210 do
                    local faderValue = currentFaderValues[i] or 0
                    oscMessage = oscMessage .. "," .. math.floor(faderValue)
                    oldValues[i] = faderValue
                end
                
                -- Add all dual color values (primary from 201-210, secondary from 101-110)
                for i = 1, 10 do
                    local primaryFader = 200 + i  -- 201-210
                    local secondaryFader = 100 + i  -- 101-110
                    
                    local primaryColor = currentColorValues[primaryFader] or "0,0,0,0"
                    local secondaryColor = currentColorValues[secondaryFader] or "0,0,0,0"
                    
                    -- Convert comma separators to semicolons and combine primary + secondary
                    local primaryColorSemicolon = string.gsub(primaryColor, ",", ";")
                    local secondaryColorSemicolon = string.gsub(secondaryColor, ",", ";")
                    local combinedColor = primaryColorSemicolon .. ";" .. secondaryColorSemicolon
                    
                    oscMessage = oscMessage .. "," .. combinedColor
                    
                    -- Update old values for both ranges
                    oldColorValues[primaryFader] = primaryColor
                    oldColorValues[secondaryFader] = secondaryColor
                end

                -- Send the single packet containing everything
                Cmd('SendOSC ' .. oscEntry .. ' "' .. oscMessage .. '"')
                Printf("Sent fader update: Page " .. destPage .. ".")
            end

            -- Send executor on/off status bundle separately so we do not spam fader updates
            if statusChanged or forceReload then
                local execMessage = "/execUpdate" .. execStatusTypeTag .. "," .. destPage

                for _, execNo in ipairs(executorsToWatch) do
                    local statusCode = currentExecStatus[execNo] or 0
                    execMessage = execMessage .. "," .. statusCode
                    oldExecStatus[execNo] = currentExecStatus[execNo]
                end

                Cmd('SendOSC ' .. oscEntry .. ' "' .. execMessage .. '"')
                Printf("Sent executor status update: Page " .. destPage .. ".")
            end
            
            forceReload = false

            -- Main loop delay
            coroutine.yield(tick)
        end
        
    elseif name == "Stop" then
        Printf("Stopping EvoFaderWing OSC...")
        SetVar(GlobalVars(), "opdateOSC", false)
        
    elseif name == "Force Reload" then
        Printf("Force reload triggered...")
        SetVar(GlobalVars(), "forceReload", true)
        
    else
        Printf("Canceled")
    end
end

return main
