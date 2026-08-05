local chain
local transcript
local SELF_TEST_PAYLOAD = 'OTCAM-CLIENT-SELFTEST-1\n'
local SELF_TEST_SIGNATURE =
    'RNaFvw+iqFul2Uv6pUX+bskQvrwS3sXjJ5PYgbInlchDfKD/WCH60R3bH5f9ekfg+gvlCAwX12zz7qEW8qx4DQ=='

local function resetEvidence()
    chain = {
        broken = false,
        error = nil,
        connectionId = nil,
        sequence = 0,
        previousSignature = '-',
        world = nil,
        generation = nil,
        session = nil,
        playerGuid = nil,
        playerName = nil
    }
    transcript = {
        active = false,
        connectionId = nil,
        sequence = 0,
        previousSignature = '-',
        digest = '-',
        world = nil,
        generation = nil,
        session = nil,
        playerGuid = nil,
        playerName = nil,
        packetHadEvidence = false,
        packetSealValid = false,
        pendingEvidenceThings = {}
    }
end

local function splitPlain(value, separator)
    local parts = {}
    local startAt = 1
    while true do
        local separatorAt = value:find(separator, startAt, true)
        if not separatorAt then
            parts[#parts + 1] = value:sub(startAt)
            return parts
        end

        parts[#parts + 1] = value:sub(startAt, separatorAt - 1)
        startAt = separatorAt + #separator
    end
end

local function markBroken(reason)
    chain.broken = true
    chain.error = reason or 'unknown validation error'
end

local function parseUnsignedInteger(value)
    if not value or not value:match('^%d+$') then
        return nil
    end
    return tonumber(value)
end

local function resolveThing(domain, locationA, locationB, locationC, locationD)
    if domain == 'M' then
        return g_map.getThing({ x = locationA, y = locationB, z = locationC }, locationD)
    elseif domain == 'C' then
        local container = g_game.getContainer(locationA)
        return container and container:getItem(locationB) or nil
    elseif domain == 'I' then
        local player = g_game.getLocalPlayer()
        return player and player:getInventoryItem(locationA) or nil
    end
    return nil
end

local function getRecordedWireValue(item)
    if item:isStackable() or item:isFluidContainer() then
        return item:getCountOrSubType()
    end

    local thingType = g_things.getThingType(item:getId(), ThingCategoryItem)
    if thingType and thingType:isSplash() then
        return item:getCountOrSubType()
    end

    -- The 7.72 wire format carries no count/subtype byte for ordinary
    -- non-stackable items. The server signs their logical amount as one,
    -- while OTClient keeps the unused internal field at zero.
    return 1
end

local function validateChain(header, signature)
    local world = parseUnsignedInteger(header[3])
    local generation = parseUnsignedInteger(header[4])
    local session = parseUnsignedInteger(header[5])
    local playerGuid = parseUnsignedInteger(header[6])
    local signedAt = parseUnsignedInteger(header[7])
    local connectionId = header[8]
    local sequence = parseUnsignedInteger(header[9])
    local previousSignature = header[10]
    local playerName = g_crypt.base64Decode(header[11])
    local entryCount = parseUnsignedInteger(header[12])

    if not world or not generation or not session or not playerGuid or not signedAt or
        not sequence or not entryCount or #connectionId ~= 32 or playerName == '' then
        return nil, 'invalid signed header'
    end

    if not connectionId:match('^[0-9a-f]+$') then
        return nil, 'invalid signed connection identifier'
    end

    if not chain.connectionId then
        if sequence ~= 1 or previousSignature ~= '-' then
            return nil, 'the signed chain does not begin at sequence 1'
        end
        chain.connectionId = connectionId
        chain.world = world
        chain.generation = generation
        chain.session = session
        chain.playerGuid = playerGuid
        chain.playerName = playerName
    elseif connectionId ~= chain.connectionId or
        world ~= chain.world or generation ~= chain.generation or session ~= chain.session or
        playerGuid ~= chain.playerGuid or playerName ~= chain.playerName or
        sequence ~= chain.sequence + 1 or previousSignature ~= chain.previousSignature then
        return nil, 'signed evidence was removed, inserted, copied, or reordered'
    end

    return {
        world = world,
        generation = generation,
        session = session,
        playerGuid = playerGuid,
        playerName = playerName,
        signedAt = signedAt,
        connectionId = connectionId,
        sequence = sequence,
        entryCount = entryCount
    }
end

local function onForensicOpcode(_, _, buffer)
    if not g_game.isPlayingRecord() then
        return
    end

    transcript.packetHadEvidence = true
    if chain.broken then
        return
    end

    local markerAt = buffer:find('\nSIG|', 1, true)
    if not markerAt then
        markBroken('missing server signature')
        return
    end

    local payload = buffer:sub(1, markerAt - 1)
    local signature = buffer:sub(markerAt + 5)
    if signature == '' or signature:find('\n', 1, true) or
        not g_crypt.verifyCamForensicSignature(payload, signature) then
        markBroken('invalid server signature')
        return
    end

    local lines = splitPlain(payload, '\n')
    local header = splitPlain(lines[1] or '', '|')
    if #header ~= 12 or header[1] ~= 'OTCAM-EVIDENCE' or header[2] ~= '1' then
        markBroken('unsupported signed evidence format')
        return
    end

    local context, contextError = validateChain(header, signature)
    if not context then
        markBroken(contextError)
        return
    end

    if #lines - 1 ~= context.entryCount then
        markBroken('signed entry count does not match the payload')
        return
    end

    local resolved = {}
    for lineIndex = 2, #lines do
        local fields = splitPlain(lines[lineIndex], '|')
        if #fields ~= 10 then
            markBroken('invalid signed item entry')
            return
        end

        local domain = fields[1]
        local locationA = parseUnsignedInteger(fields[2])
        local locationB = parseUnsignedInteger(fields[3])
        local locationC = parseUnsignedInteger(fields[4])
        local locationD = parseUnsignedInteger(fields[5])
        local serverItemId = parseUnsignedInteger(fields[6])
        local clientItemId = parseUnsignedInteger(fields[7])
        local wireValue = parseUnsignedInteger(fields[8])
        local instanceId = g_crypt.base64Decode(fields[9])
        local description = g_crypt.base64Decode(fields[10])

        if (domain ~= 'M' and domain ~= 'C' and domain ~= 'I') or
            not locationA or not locationB or not locationC or not locationD or
            not serverItemId or not clientItemId or not wireValue or
            not instanceId:match('^[0-9a-f][0-9a-f]+$') or #instanceId ~= 32 or
            description == '' then
            markBroken('invalid signed item values')
            return
        end

        local resolvedOk, thing = pcall(
            resolveThing, domain, locationA, locationB, locationC, locationD)
        if not resolvedOk then
            markBroken('the client failed to resolve a signed item: ' .. tostring(thing))
            return
        end

        if not thing or not thing:isItem() then
            markBroken(string.format(
                'signed item is missing at %s:%d,%d,%d,%d',
                domain, locationA, locationB, locationC, locationD))
            return
        end

        local actualWireValue = getRecordedWireValue(thing)
        if thing:getId() ~= clientItemId or actualWireValue ~= wireValue then
            markBroken(string.format(
                'signed item does not match the recorded game packet at %s:%d,%d,%d,%d ' ..
                '(expected client_id=%d value=%d, found client_id=%d value=%d)',
                domain, locationA, locationB, locationC, locationD,
                clientItemId, wireValue, thing:getId(), actualWireValue))
            return
        end

        resolved[#resolved + 1] = {
            thing = thing,
            evidence = {
                description = description,
                instanceId = instanceId,
                serverItemId = serverItemId,
                clientItemId = clientItemId,
                world = context.world,
                generation = context.generation,
                session = context.session,
                playerGuid = context.playerGuid,
                playerName = context.playerName,
                signedAt = context.signedAt,
                connectionId = context.connectionId,
                sequence = context.sequence
            }
        }
    end

    for _, record in ipairs(resolved) do
        -- LuaInterface creates a fresh userdata wrapper whenever the same
        -- C++ ThingPtr crosses into Lua. A userdata-keyed table therefore
        -- cannot find the item again during Look. Arbitrary object fields are
        -- stored by LuaObject itself and remain attached to the real C++
        -- object across wrappers.
        record.thing.camForensicEvidence = record.evidence
        transcript.pendingEvidenceThings[#transcript.pendingEvidenceThings + 1] = record.thing
    end

    chain.sequence = context.sequence
    chain.previousSignature = signature
end

local function onTranscriptSeal(buffer, packetPrefix, isLast, duplicateSeal)
    if chain.broken then
        return
    end

    if duplicateSeal then
        markBroken('more than one transcript seal exists in the same packet')
        return
    end

    if not isLast then
        markBroken('the transcript seal is not the final message in its packet')
        return
    end

    local markerAt = buffer:find('\nSIG|', 1, true)
    if not markerAt then
        markBroken('missing packet transcript signature')
        return
    end

    local payload = buffer:sub(1, markerAt - 1)
    local signature = buffer:sub(markerAt + 5)
    if signature == '' or signature:find('\n', 1, true) or
        not g_crypt.verifyCamForensicSignature(payload, signature) then
        markBroken('invalid packet transcript signature')
        return
    end

    local header = splitPlain(payload, '|')
    if #header ~= 14 or header[1] ~= 'OTCAM-TRANSCRIPT' or header[2] ~= '1' then
        markBroken('unsupported packet transcript format')
        return
    end

    local kind = header[3]
    local world = parseUnsignedInteger(header[4])
    local generation = parseUnsignedInteger(header[5])
    local session = parseUnsignedInteger(header[6])
    local playerGuid = parseUnsignedInteger(header[7])
    local signedAt = parseUnsignedInteger(header[8])
    local connectionId = header[9]
    local sequence = parseUnsignedInteger(header[10])
    local previousSignature = header[11]
    local playerName = g_crypt.base64Decode(header[12])
    local packetLength = parseUnsignedInteger(header[13])
    local packetDigest = header[14]

    if (kind ~= 'START' and kind ~= 'EVIDENCE') or
        not world or not generation or not session or not playerGuid or not signedAt or
        not sequence or not packetLength or packetLength ~= #packetPrefix or
        #connectionId ~= 32 or not connectionId:match('^[0-9a-f]+$') or
        playerName == '' or #packetDigest ~= 64 or
        not packetDigest:match('^[0-9a-f]+$') then
        markBroken('invalid packet transcript values')
        return
    end

    local expectedDigest = g_crypt.sha256Hex(
        string.format('OTCAM-PACKET|1|%s|%d\n', transcript.digest, #packetPrefix) ..
        packetPrefix)
    if expectedDigest == '' or expectedDigest ~= packetDigest then
        markBroken('a recorded packet was edited, inserted, removed, or transplanted')
        return
    end

    if kind == 'START' then
        if transcript.active or sequence ~= 1 or previousSignature ~= '-' then
            markBroken('invalid transcript start')
            return
        end
        transcript.active = true
        transcript.connectionId = connectionId
        transcript.world = world
        transcript.generation = generation
        transcript.session = session
        transcript.playerGuid = playerGuid
        transcript.playerName = playerName
    elseif not transcript.active or not transcript.packetHadEvidence or
        connectionId ~= transcript.connectionId or
        world ~= transcript.world or generation ~= transcript.generation or
        session ~= transcript.session or playerGuid ~= transcript.playerGuid or
        playerName ~= transcript.playerName or sequence ~= transcript.sequence + 1 or
        previousSignature ~= transcript.previousSignature then
        markBroken('the authenticated packet transcript is discontinuous')
        return
    end

    for _, thing in ipairs(transcript.pendingEvidenceThings) do
        local evidence = thing.camForensicEvidence
        if not evidence or
            evidence.world ~= world or evidence.generation ~= generation or
            evidence.session ~= session or evidence.playerGuid ~= playerGuid or
            evidence.playerName ~= playerName then
            markBroken('item evidence does not belong to this authenticated transcript')
            return
        end
    end

    transcript.sequence = sequence
    transcript.previousSignature = signature
    transcript.digest = packetDigest
    transcript.packetSealValid = true

    for _, thing in ipairs(transcript.pendingEvidenceThings) do
        local evidence = thing.camForensicEvidence
        evidence.transcriptSequence = sequence
        evidence.transcriptDigest = packetDigest
    end
end

local function onTranscriptPacketEnd(packetBody, sealSeen, parseComplete)
    if not chain.broken and not parseComplete and
        (transcript.active or transcript.packetHadEvidence or sealSeen) then
        markBroken('the recorded packet could not be parsed completely')
    end

    if not chain.broken then
        if sealSeen then
            if not transcript.packetSealValid then
                markBroken('the packet transcript seal was not accepted')
            end
        elseif transcript.packetHadEvidence then
            markBroken('signed item evidence is missing its packet transcript seal')
        elseif transcript.active then
            transcript.digest = g_crypt.sha256Hex(
                string.format('OTCAM-PACKET|1|%s|%d\n', transcript.digest, #packetBody) ..
                packetBody)
            if transcript.digest == '' then
                markBroken('unable to hash the recorded packet transcript')
            end
        end
    end

    transcript.packetHadEvidence = false
    transcript.packetSealValid = false
    transcript.pendingEvidenceThings = {}
end

local function formatSignedTime(milliseconds)
    local seconds = math.floor(milliseconds / 1000)
    local millis = milliseconds % 1000
    return string.format('%s.%03d', os.date('%d/%m/%Y %H:%M:%S', seconds), millis)
end

function look(thing)
    if not g_game.isPlayingRecord() then
        return
    end

    local message
    local title
    if chain.broken then
        title = 'CAM FORENSE INVALIDA'
        message = string.format(
            'CAM FORENSE INVALIDA: a cadeia assinada foi adulterada ou esta corrompida (%s). ' ..
            'O Look foi recusado.',
            chain.error or 'erro desconhecido')
    elseif not transcript.active then
        title = 'CAM SEM EVIDENCIA'
        message =
            'CAM: esta gravacao nao possui um fluxo de pacotes autenticado pelo servidor. ' ..
            'Ela pode ser antiga ou ter sido criada por um cliente incompatível. O Look foi recusado.'
    elseif not thing or not thing:isItem() then
        title = 'CAM SEM EVIDENCIA'
        message = 'CAM: o Look forense esta disponivel apenas para itens autenticados pelo servidor.'
    else
        local evidence = thing.camForensicEvidence
        if not evidence or not evidence.transcriptDigest or
            evidence.connectionId ~= chain.connectionId or
            evidence.sequence > chain.sequence or
            evidence.world ~= transcript.world or
            evidence.generation ~= transcript.generation or
            evidence.session ~= transcript.session or
            evidence.playerGuid ~= transcript.playerGuid or
            evidence.playerName ~= transcript.playerName or
            evidence.transcriptSequence > transcript.sequence then
            title = 'CAM SEM EVIDENCIA'
            message =
                'CAM: este item nao possui evidencia assinada valida. Ele pode ser um item sem instance_id, ' ..
                'uma CAM antiga ou dados inseridos/editados manualmente. O Look foi recusado.'
        else
            title = 'CAM FORENSE - ASSINATURA VALIDA'
            message = string.format(
                '%s\n[EVIDENCIA CAM AUTENTICA - assinatura Ed25519 valida]' ..
                '\nObservador original: %s (GUID %d)' ..
                '\nHorario original do servidor: %s' ..
                '\nMundo/geracao/sessao: %d/%d/%d' ..
                '\nConexao/evidencia: %s/%d' ..
                '\nFluxo autenticado: seq. %d, hash %s',
                evidence.description,
                evidence.playerName,
                evidence.playerGuid,
                formatSignedTime(evidence.signedAt),
                evidence.world,
                evidence.generation,
                evidence.session,
                evidence.connectionId,
                evidence.sequence,
                evidence.transcriptSequence,
                evidence.transcriptDigest)
        end
    end

    modules.game_textmessage.displayMessage(MessageModes.Look, message)
    displayInfoBox(title, message)
end

function init()
    if not g_crypt.verifyCamForensicSignature(SELF_TEST_PAYLOAD, SELF_TEST_SIGNATURE) or
        g_crypt.verifyCamForensicSignature(SELF_TEST_PAYLOAD .. 'tampered', SELF_TEST_SIGNATURE) then
        error('CAM forensic public-key verification self-test failed')
    end
    if g_crypt.sha256Hex('abc') ~=
        'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad' then
        error('CAM forensic packet-hash self-test failed')
    end

    resetEvidence()
    ProtocolGame.registerExtendedOpcode(ExtendedIds.CamForensics, onForensicOpcode)
    connect(g_game, {
        onGameStart = resetEvidence,
        onGameEnd = resetEvidence,
        onCamForensicLook = look,
        onCamTranscriptSeal = onTranscriptSeal,
        onCamTranscriptPacketEnd = onTranscriptPacketEnd
    })
end

function terminate()
    ProtocolGame.unregisterExtendedOpcode(ExtendedIds.CamForensics)
    disconnect(g_game, {
        onGameStart = resetEvidence,
        onGameEnd = resetEvidence,
        onCamForensicLook = look,
        onCamTranscriptSeal = onTranscriptSeal,
        onCamTranscriptPacketEnd = onTranscriptPacketEnd
    })
    resetEvidence()
end
