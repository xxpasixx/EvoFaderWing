-- EVOFaderWing v0.3 Lua script for syncing EVOFaderWing using OSC
-- Sends execUpdate (page + faders + exec status) and colorUpdate for all Execs and Faders
-- OSC sent when data has changed or after autoResendInterval
--
-- Since v0.3 we now track all Executors status and color, including Proxy Execs that are created when expanding Execs
-- Compatiable with Sequence, Macro, and Plugin objects only (for now)
--
-- Default autoResendInterval is 300 (15 seconds) 
--
-- Special thanks to xxpasixx for his pam-osc code which I modified for my project
-- GPL3


local function StartGui()
    -- Check current status
    local isRunning = GetVar(GlobalVars(), "opdateOSC") == true
    
    local descTable = {
        title = "EvoFaderWingv0.3 Control",
        caller = GetFocusDisplay(),
        add_args = {FilterSupport = "Yes"},
    }
    
    if isRunning then
        descTable.items = {"Stop", "Force Reload"}
        descTable.message = "EvoFaderWingv0.3 is RUNNING"
    else
        descTable.items = {"Start", "Force Reload"}  
        descTable.message = "EvoFaderWingv0.3 is STOPPED"
    end

    local index, name = PopupInput(descTable)
    return index, name
end

function main()
    local index, name = StartGui()

    if name == "Start" then
        Printf("Starting EvoFaderWingv0.3...")
        
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

        -- Set the default color values for all exec ranges
        for i = 101, 110 do oldColorValues[i] = "0,0,0,0" end
        for i = 201, 210 do oldColorValues[i] = "0,0,0,0" end
        for i = 301, 310 do oldColorValues[i] = "0,0,0,0" end
        for i = 401, 410 do oldColorValues[i] = "0,0,0,0" end

        -- The speed to check executors
        local tick = 1 / 20 -- 1/20 second = 50ms
        local resendTick = 0
        local autoResendInterval = 300 -- ticks (15 seconds at 50ms per tick)
        -- New packet layouts:
        --   /execUpdate: page + 10 fader ints + 40 executor status ints
        --   /colorUpdate: page + 40 color strings (101-410)
        local execUpdateTypeTag = "," .. string.rep("i", 1 + 10 + #executorsToWatch)
        local colorUpdateTypeTag = "," .. "i" .. string.rep("s", #executorsToWatch)
        local DEBUG_PROXY = false

        local function getAppearanceColor(sequence)
            local apper = sequence["APPEARANCE"]
            if apper ~= nil then
                return apper['BACKR'] .. "," .. apper['BACKG'] .. "," .. apper['BACKB'] .. "," .. apper['BACKALPHA']
            else
                return "255,255,255,255"
            end
        end

        local function extractAppearance(target)
            if target == nil then return nil end
            local ok, ap = pcall(function() return target["APPEARANCE"] end)
            if ok and ap ~= nil then return ap end
            return nil
        end

        local function getName(obj)
            if obj == nil then return "nil" end
            local ok, res = pcall(function() return obj:Name() end)
            if ok and res ~= nil then return res end
            local ok2, res2 = pcall(function() return obj.Name end)
            if ok2 and res2 ~= nil then return res2 end
            return "?"
        end

        local function getType(obj)
            local ok, res = pcall(function() return obj:Type() end)
            if ok then return res end
            return nil
        end

        local function isSequence(obj)
            if obj == nil then return false end
            local t = getType(obj)
            if t and string.lower(tostring(t)) == "sequence" then return true end
            local n = getName(obj)
            if n and string.find(string.lower(tostring(n)), "sequence") then
                return true
            end
            return false
        end

        local function describe(obj)
            if obj == nil then return "nil" end
            return string.format("%s (%s)", tostring(getName(obj)), tostring(getType(obj) or type(obj)))
        end

        local function getSequenceName(seqObj)
            if seqObj ~= nil and isSequence(seqObj) then
                local n = getName(seqObj)
                if n ~= nil and n ~= "?" then
                    return n
                end
            end
            return nil
        end

        local function getSequenceKey(seqObj, execHandle)
            -- Use the sequence name if available; otherwise fall back to the executor handle name
            local n = getSequenceName(seqObj)
            if n ~= nil then return n end
            local h = getName(execHandle)
            if h ~= nil and h ~= "?" then
                return h
            end
            return nil
        end

        local function findSequenceByName(name)
            if name == nil or name == "?" then return nil end
            local seqPool = DataPool().Sequences
            if seqPool == nil then return nil end
            local okCount, total = pcall(function() return seqPool:Count() end)
            if not okCount or not total then return nil end
            for i = 1, total do
                local okSeq, seq = pcall(function() return seqPool[i] end)
                if okSeq and seq ~= nil then
                    local n = getName(seq)
                    if n == name then
                        return seq
                    end
                end
            end
            return nil
        end

        local function findPoolObjectByName(pool, name)
            if pool == nil or name == nil or name == "?" then return nil end
            local okCount, total = pcall(function() return pool:Count() end)
            if not okCount or not total then return nil end
            for i = 1, total do
                local okObj, obj = pcall(function() return pool[i] end)
                if okObj and obj ~= nil and getName(obj) == name then
                    return obj
                end
            end
            return nil
        end

        local function resolveObjectByName(name)
            if name == nil or name == "?" then return nil end
            local seq = findSequenceByName(name)
            if seq ~= nil then return seq end
            local macro = findPoolObjectByName(DataPool().Macros, name)
            if macro ~= nil then return macro end
            local plugin = findPoolObjectByName(DataPool().Plugins, name)
            if plugin ~= nil then return plugin end
            return nil
        end

        local function findHostExecForSequence(seqObj, currentPage)
            if seqObj == nil or currentPage == nil then
                return nil
            end
            local page = DataPool().Pages[currentPage.index]
            if page == nil then
                return nil
            end
            local okCount, childCount = pcall(function() return page:Count() end)
            if not okCount or not childCount then
                return nil
            end
            for idx = 1, childCount do
                local okHandle, handle = pcall(function() return page[idx] end)
                if okHandle and handle ~= nil then
                    local hObj = handle.Object
                    if hObj ~= nil and hObj == seqObj then
                        return handle
                    end
                    if isSequence(handle) and handle == seqObj then
                        return handle
                    end
                end
            end
            return nil
        end

        local function readExecutorState(execNo, wantsValue, wantsColor, hostCache)
            -- Pull executor info directly (avoids scanning the whole page every tick)
            local execHandle = GetExecutor(execNo)
            if execHandle == nil then
                if DEBUG_PROXY then
                    Printf("[ProxyDebug] Exec %d handle=nil", execNo)
                end
                return 0, "0,0,0,0", 0, nil, false
            end

            if DEBUG_PROXY then
                Printf("[ProxyDebug] Exec %d start handle=%s obj=%s", execNo, describe(execHandle), describe(execHandle.Object))
            end

            -- Try to resolve proxy by looking at the page slot (page[execNo]) or a child with matching name
            local proxyResolvedObject = nil
            local currentPage = CurrentExecPage()
            if currentPage ~= nil then
                local page = DataPool().Pages[currentPage.index]
                if page ~= nil then
                    local okSlot, slotHandle = pcall(function() return page[execNo] end)
                    if okSlot and slotHandle ~= nil then
                        if isSequence(slotHandle) then
                            proxyResolvedObject = slotHandle
                        elseif slotHandle.Object ~= nil then
                            proxyResolvedObject = slotHandle.Object
                        end
                    end
                end
                if proxyResolvedObject == nil and currentPage.Children and execHandle.Object == nil then
                    local ok, children = pcall(function() return currentPage:Children() end)
                    if ok and type(children) == "table" then
                        local needle = tostring(execNo)
                        for _, child in ipairs(children) do
                            if child ~= nil and child.Object ~= nil then
                                local okName, n = pcall(function() return child:Name() end)
                                local name = okName and n or ""
                                if name ~= "" and string.find(name, needle, 1, true) then
                                    proxyResolvedObject = child.Object
                                    break
                                end
                            end
                        end
                    end
                end
            end
            if DEBUG_PROXY and handleIsSequence then
                Printf("[ProxyDebug] Exec %d seqHandle=%s proxyObj=%s hostExec=%s", execNo, describe(execHandle), describe(proxyResolvedObject), describe(findHostExecForSequence(seqObj, currentPage)))
            end

            local faderValue = 0
            if wantsValue then
                local okFader, fv = pcall(function() return execHandle:GetFader({token = "FaderMaster", faderDisabled = false}) end)
                if okFader and fv then
                    faderValue = fv
                else
                    faderValue = 0
                end
            end

            local colorValue = "0,0,0,0"
            local isOn = false
            local handleIsSequence = isSequence(execHandle)
            local isPopulated = handleIsSequence or execHandle.Object ~= nil or proxyResolvedObject ~= nil

            local obj = nil
            local hostExec = nil
            if handleIsSequence then
                obj = execHandle
            else
                obj = execHandle.Object or proxyResolvedObject
            end

            local handleName = getName(execHandle)
            local seqObj = nil
            if handleIsSequence then
                seqObj = execHandle
            elseif isSequence(proxyResolvedObject) then
                seqObj = proxyResolvedObject
            elseif isSequence(execHandle.Object) then
                seqObj = execHandle.Object
                obj = obj or seqObj
            end
            -- Always try name-based resolution so default "Sequence #" and macros/plugins behave the same
            local resolvedByName = nil
            if handleName ~= nil and handleName ~= "?" then
                resolvedByName = resolveObjectByName(handleName)
                -- Always prefer a real pool sequence when found, even if execHandle is already a sequence handle
                local seqByName = findSequenceByName(handleName)
                if seqByName ~= nil then
                    seqObj = seqByName
                elseif resolvedByName ~= nil and isSequence(resolvedByName) then
                    seqObj = resolvedByName
                end
            end

            -- Prefer resolved objects by name for appearance/status when available
            if resolvedByName ~= nil then
                obj = resolvedByName
                if seqObj == nil and isSequence(resolvedByName) then
                    seqObj = resolvedByName
                end
                isPopulated = true
            elseif seqObj ~= nil then
                obj = obj or seqObj
            end

            local seqKey = getSequenceKey(seqObj, execHandle)
            if seqKey == nil then
                seqKey = handleName
            end

            if seqObj ~= nil then
                hostExec = findHostExecForSequence(seqObj, currentPage)
                if hostExec ~= nil then
                    if hostExec.Object ~= nil then
                        obj = hostExec.Object
                    else
                        obj = hostExec
                    end
                    isPopulated = true
                    if DEBUG_PROXY then
                        Printf("[ProxyDebug] Exec %d sequence %s using host exec obj %s", execNo, tostring(getName(seqObj)), tostring(getName(obj)))
                    end
                elseif DEBUG_PROXY then
                    Printf("[ProxyDebug] Exec %d sequence %s has no host exec on page", execNo, tostring(getName(seqObj)))
                end
            end

            if DEBUG_PROXY and handleIsSequence then
                Printf("[ProxyDebug] Exec %d handleIsSequence=%s obj=%s proxyObj=%s pop=%s", execNo, tostring(handleIsSequence), describe(obj), describe(proxyResolvedObject), tostring(isPopulated))
            end

            if obj ~= nil then
                if wantsColor then
                    -- Prefer sequence appearance, then executor appearance, then object appearance
                    local ap = extractAppearance(seqObj) or extractAppearance(hostExec) or extractAppearance(obj) or extractAppearance(execHandle)
                    if ap ~= nil then
                        colorValue = ap['BACKR'] .. "," .. ap['BACKG'] .. "," .. ap['BACKB'] .. "," .. ap['BACKALPHA']
                    else
                        colorValue = getAppearanceColor(obj)
                    end
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

            -- Cache color/status by sequence key so proxies can reuse it
            if hostCache ~= nil and seqKey ~= nil then
                hostCache[seqKey] = { color = colorValue, status = statusCode }
            end

            local isProxy = handleIsSequence and execHandle.Object == nil
            return faderValue, colorValue, statusCode, seqObj, isProxy, seqKey
        end

        Printf("start EvoFaderWingv0.3 - fader values/colors + executor status (101-410)")
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
            local execColorChanged = false

            -- Check Page
            local myPage = CurrentExecPage()
            if myPage.index ~= destPage then
                destPage = myPage.index
                -- Reset fader values (201-210)
                for i = 201, 210 do
                    oldValues[i] = 000
                end
                -- Reset color values for all ranges
                for i = 101, 110 do oldColorValues[i] = "0,0,0,0" end
                for i = 201, 210 do oldColorValues[i] = "0,0,0,0" end
                for i = 301, 310 do oldColorValues[i] = "0,0,0,0" end
                for i = 401, 410 do oldColorValues[i] = "0,0,0,0" end
                for _, execNo in ipairs(executorsToWatch) do
                    oldExecStatus[execNo] = 0
                end
                forceReload = true
                faderDataChanged = true
                statusChanged = true
                execColorChanged = true
            end

            -- Collect all current values
            local currentFaderValues = {}
            local currentColorValues = {}
            local currentExecStatus = {}

            local hostCache = {}     -- sequence key -> {color,status}
            local execSeqNames = {} -- execNo -> sequence key
            local execIsProxy = {}   -- execNo -> bool

            for _, execNo in ipairs(executorsToWatch) do
                local wantsValue = execNo >= 201 and execNo <= 210
                local wantsColor = true -- capture appearance for all execs

                local faderValue, colorValue, statusCode, seqObj, isProxy, seqName = readExecutorState(execNo, wantsValue, wantsColor, hostCache)
                execSeqNames[execNo] = seqName
                execIsProxy[execNo] = isProxy

                if wantsValue then
                    currentFaderValues[execNo] = faderValue
                    if oldValues[execNo] ~= faderValue then
                        faderDataChanged = true
                    end
                end

                if wantsColor then
                    currentColorValues[execNo] = colorValue
                    if oldColorValues[execNo] ~= colorValue then
                        if (execNo >= 101 and execNo <= 110) or (execNo >= 201 and execNo <= 210) then
                            faderDataChanged = true
                        else
                            execColorChanged = true
                        end
                    end
                end

                currentExecStatus[execNo] = statusCode
                if oldExecStatus[execNo] ~= statusCode then
                    statusChanged = true
                end

            end

            -- Second pass: apply host color/status to execs tied to a sequence; always sync color to host
            for execNo, statusCode in pairs(currentExecStatus) do
                if execIsProxy[execNo] then
                    local seqName = execSeqNames[execNo]
                    local host = seqName and hostCache[seqName] or nil
                    if host ~= nil then
                        if currentColorValues[execNo] ~= host.color then
                            currentColorValues[execNo] = host.color
                            execColorChanged = true
                        end
                        if statusCode ~= host.status then
                            currentExecStatus[execNo] = host.status
                            statusChanged = true
                        end
                    end
                end
            end

            -- Send bundled executor update: page + 10 faders + 40 statuses
            if faderDataChanged or statusChanged or forceReload then
                local execMessage = "/execUpdate" .. execUpdateTypeTag .. "," .. destPage

                -- Add fader values (201-210) as ints
                for i = 201, 210 do
                    local faderValue = currentFaderValues[i] or 0
                    execMessage = execMessage .. "," .. math.floor(faderValue)
                    oldValues[i] = faderValue
                end

                -- Add executor status codes (101-410)
                for _, execNo in ipairs(executorsToWatch) do
                    local statusCode = currentExecStatus[execNo] or 0
                    execMessage = execMessage .. "," .. statusCode
                    oldExecStatus[execNo] = currentExecStatus[execNo]
                end

                Cmd('SendOSC ' .. oscEntry .. ' "' .. execMessage .. '"')
                Printf("Sent exec update: Page " .. destPage .. ".")
            end

            -- Send all colors together as individual args (101-410)
            if execColorChanged or faderDataChanged or forceReload then
                local colorMessage = "/colorUpdate" .. colorUpdateTypeTag .. "," .. destPage
                for _, execNo in ipairs(executorsToWatch) do
                    local c = currentColorValues[execNo] or "0,0,0,0"
                    local cSemi = string.gsub(c, ",", ";")
                    colorMessage = colorMessage .. "," .. cSemi
                    oldColorValues[execNo] = c
                end

                Cmd('SendOSC ' .. oscEntry .. ' "' .. colorMessage .. '"')
                Printf("Sent color update: Page " .. destPage .. ".")
            end
            
            forceReload = false

            -- Main loop delay
            coroutine.yield(tick)
        end
        
    elseif name == "Stop" then
        Printf("Stopping EvoFaderWingv0.3...")
        SetVar(GlobalVars(), "opdateOSC", false)
        
    elseif name == "Force Reload" then
        Printf("Force reload triggered...")
        SetVar(GlobalVars(), "forceReload", true)
        
    else
        Printf("Canceled")
    end
end

return main
