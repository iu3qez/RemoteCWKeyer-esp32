-- CWNet Protocol Dissector for Wireshark
-- Author: IU3QEZ Keyer Project
-- Date: 2026-01-12
-- Based on: DL4YHF CWNet protocol analysis
--
-- Installation:
--   1. Copy this file to Wireshark plugins directory:
--      - Linux: ~/.local/lib/wireshark/plugins/
--      - Windows: %APPDATA%\Wireshark\plugins\
--      - macOS: ~/.config/wireshark/plugins/
--   2. Restart Wireshark or Analyze → Reload Lua Plugins
--
-- Usage:
--   Filter: cwnet
--   Port: 7355 (default, or configure in protocol preferences)

-- Protocol declaration
local cwnet_proto = Proto("cwnet", "CWNet Remote CW Keyer Protocol")

-- Command types
local cmd_types = {
    [0x01] = "CONNECT",
    [0x02] = "DISCONNECT",
    [0x03] = "PING",
    [0x04] = "PRINT",
    [0x05] = "TX_INFO",
    [0x06] = "RIGCTLD",
    [0x10] = "MORSE",
    [0x11] = "AUDIO",
    [0x12] = "VORBIS",
    [0x14] = "CI_V",
    [0x15] = "SPECTRUM",
    [0x16] = "FREQ_REPORT",
    [0x18] = "PARAM_INTEGER",
    [0x19] = "PARAM_DOUBLE",
    [0x1A] = "PARAM_STRING",
    [0x20] = "METER_REPORT",
    [0x21] = "POTI_REPORT",
    [0x31] = "TUNNEL_1",
    [0x32] = "TUNNEL_2",
    [0x33] = "TUNNEL_3",
}

-- Ping types
local ping_types = {
    [0] = "REQUEST",
    [1] = "RESPONSE_1",
    [2] = "RESPONSE_2",
}

-- Permissions
local permission_flags = {
    [0x01] = "TALK",
    [0x02] = "TRANSMIT",
    [0x04] = "CTRL_RIG",
    [0x08] = "ADMIN",
}

-- Protocol fields
local pf = {
    -- Frame header
    cmd_byte     = ProtoField.uint8("cwnet.cmd_byte", "Command Byte", base.HEX),
    cmd_type     = ProtoField.uint8("cwnet.cmd_type", "Command Type", base.HEX, cmd_types, 0x3F),
    block_type   = ProtoField.uint8("cwnet.block_type", "Block Type", base.DEC, {
        [0] = "No Payload",
        [1] = "Short Block (1 byte len)",
        [2] = "Long Block (2 byte len)",
        [3] = "Reserved",
    }, 0xC0),
    payload_len  = ProtoField.uint16("cwnet.payload_len", "Payload Length", base.DEC),

    -- CONNECT fields
    username     = ProtoField.string("cwnet.username", "Username"),
    callsign     = ProtoField.string("cwnet.callsign", "Callsign"),
    permissions  = ProtoField.uint32("cwnet.permissions", "Permissions", base.HEX),
    perm_talk    = ProtoField.bool("cwnet.perm.talk", "Talk", 8, nil, 0x01),
    perm_tx      = ProtoField.bool("cwnet.perm.transmit", "Transmit", 8, nil, 0x02),
    perm_rig     = ProtoField.bool("cwnet.perm.ctrl_rig", "Control Rig", 8, nil, 0x04),
    perm_admin   = ProtoField.bool("cwnet.perm.admin", "Admin", 8, nil, 0x08),

    -- PING fields
    ping_type    = ProtoField.uint8("cwnet.ping.type", "Ping Type", base.DEC, ping_types),
    ping_id      = ProtoField.uint8("cwnet.ping.id", "Ping ID", base.DEC),
    ping_t0      = ProtoField.int32("cwnet.ping.t0", "T0 (requester)", base.DEC),
    ping_t1      = ProtoField.int32("cwnet.ping.t1", "T1 (responder 1)", base.DEC),
    ping_t2      = ProtoField.int32("cwnet.ping.t2", "T2 (responder 2)", base.DEC),
    ping_rtt     = ProtoField.int32("cwnet.ping.rtt", "Round-Trip Time (ms)", base.DEC),

    -- MORSE fields
    morse_byte   = ProtoField.uint8("cwnet.morse.byte", "Morse Byte", base.HEX),
    morse_key    = ProtoField.bool("cwnet.morse.key", "Key Down", 8, {"DOWN", "UP"}, 0x80),
    morse_time   = ProtoField.uint8("cwnet.morse.time_enc", "Time (encoded)", base.HEX, nil, 0x7F),
    morse_time_ms= ProtoField.uint16("cwnet.morse.time_ms", "Time (ms)", base.DEC),

    -- AUDIO fields
    audio_samples= ProtoField.uint16("cwnet.audio.samples", "Sample Count", base.DEC),
    audio_duration_ms = ProtoField.float("cwnet.audio.duration_ms", "Duration (ms)"),

    -- PRINT fields
    print_text   = ProtoField.string("cwnet.print.text", "Text"),

    -- Generic
    payload      = ProtoField.bytes("cwnet.payload", "Payload"),
}

cwnet_proto.fields = pf

-- Expert info
local ef = {
    incomplete = ProtoExpert.new("cwnet.incomplete", "Incomplete frame",
        expert.group.MALFORMED, expert.severity.WARN),
    unknown_cmd = ProtoExpert.new("cwnet.unknown_cmd", "Unknown command",
        expert.group.UNDECODED, expert.severity.NOTE),
    sync_ping = ProtoExpert.new("cwnet.sync_ping", "Timer sync point",
        expert.group.SEQUENCE, expert.severity.CHAT),
}

cwnet_proto.experts = ef

-- Decode 7-bit timestamp to milliseconds
local function decode_7bit_timestamp(ts)
    ts = bit.band(ts, 0x7F)
    if ts <= 0x1F then
        return ts
    elseif ts <= 0x3F then
        return 32 + 4 * (ts - 0x20)
    else
        return 157 + 16 * (ts - 0x40)
    end
end

-- Format permissions as string
local function format_permissions(perm)
    local parts = {}
    for bit_val, name in pairs(permission_flags) do
        if bit.band(perm, bit_val) ~= 0 then
            table.insert(parts, name)
        end
    end
    if #parts == 0 then
        return "NONE"
    end
    return table.concat(parts, "+")
end

-- Dissect CONNECT payload
local function dissect_connect(tvb, pinfo, tree, offset, payload_len)
    if payload_len < 92 then
        tree:add_proto_expert_info(ef.incomplete)
        return
    end

    local username = tvb(offset, 44):stringz()
    local callsign = tvb(offset + 44, 44):stringz()
    local permissions = tvb(offset + 88, 4):le_uint()

    tree:add(pf.username, tvb(offset, 44), username)
    tree:add(pf.callsign, tvb(offset + 44, 44), callsign)

    local perm_tree = tree:add(pf.permissions, tvb(offset + 88, 4), permissions)
    perm_tree:append_text(" (" .. format_permissions(permissions) .. ")")
    perm_tree:add(pf.perm_talk, tvb(offset + 88, 1))
    perm_tree:add(pf.perm_tx, tvb(offset + 88, 1))
    perm_tree:add(pf.perm_rig, tvb(offset + 88, 1))
    perm_tree:add(pf.perm_admin, tvb(offset + 88, 1))

    pinfo.cols.info:append(" [" .. callsign .. "] perm=" .. format_permissions(permissions))
end

-- Dissect PING payload
local function dissect_ping(tvb, pinfo, tree, offset, payload_len)
    if payload_len < 16 then
        tree:add_proto_expert_info(ef.incomplete)
        return
    end

    local ping_type = tvb(offset, 1):uint()
    local ping_id = tvb(offset + 1, 1):uint()
    local t0 = tvb(offset + 4, 4):le_int()
    local t1 = tvb(offset + 8, 4):le_int()
    local t2 = tvb(offset + 12, 4):le_int()

    tree:add(pf.ping_type, tvb(offset, 1))
    tree:add(pf.ping_id, tvb(offset + 1, 1))
    tree:add(pf.ping_t0, tvb(offset + 4, 4), t0):append_text(" ms")
    tree:add(pf.ping_t1, tvb(offset + 8, 4), t1):append_text(" ms")
    tree:add(pf.ping_t2, tvb(offset + 12, 4), t2):append_text(" ms")

    local type_str = ping_types[ping_type] or "UNKNOWN"
    pinfo.cols.info:append(" " .. type_str .. " id=" .. ping_id)

    if ping_type == 0 then
        -- REQUEST: This is a sync point!
        tree:add_proto_expert_info(ef.sync_ping, "Client should call timer_sync_to_server(t0=" .. t0 .. ")")
        pinfo.cols.info:append(" t0=" .. t0 .. " [SYNC POINT]")
    elseif ping_type == 2 and t2 > 0 and t0 > 0 then
        -- RESPONSE_2: Calculate RTT
        local rtt = t2 - t0
        tree:add(pf.ping_rtt, rtt):set_generated()
        pinfo.cols.info:append(" RTT=" .. rtt .. "ms")
    end
end

-- Dissect MORSE payload
local function dissect_morse(tvb, pinfo, tree, offset, payload_len)
    local total_ms = 0
    local events = {}

    for i = 0, payload_len - 1 do
        local byte = tvb(offset + i, 1):uint()
        local key_down = bit.band(byte, 0x80) ~= 0
        local time_enc = bit.band(byte, 0x7F)
        local time_ms = decode_7bit_timestamp(time_enc)

        local morse_tree = tree:add(pf.morse_byte, tvb(offset + i, 1))
        morse_tree:add(pf.morse_key, tvb(offset + i, 1))
        morse_tree:add(pf.morse_time, tvb(offset + i, 1))
        morse_tree:add(pf.morse_time_ms, time_ms):set_generated()

        local state = key_down and "DOWN" or "UP"
        morse_tree:append_text(" [" .. state .. " after " .. time_ms .. "ms]")

        total_ms = total_ms + time_ms
        table.insert(events, (key_down and "v" or "^") .. time_ms)
    end

    pinfo.cols.info:append(" " .. payload_len .. " events, " .. total_ms .. "ms total")
    pinfo.cols.info:append(" [" .. table.concat(events, ",") .. "]")
end

-- Dissect AUDIO payload
local function dissect_audio(tvb, pinfo, tree, offset, payload_len)
    tree:add(pf.audio_samples, payload_len)

    local duration_ms = (payload_len / 8000.0) * 1000.0
    tree:add(pf.audio_duration_ms, duration_ms):set_generated()

    pinfo.cols.info:append(" " .. payload_len .. " samples (" .. string.format("%.1f", duration_ms) .. "ms)")
end

-- Dissect PRINT payload
local function dissect_print(tvb, pinfo, tree, offset, payload_len)
    local text = tvb(offset, payload_len):string()
    tree:add(pf.print_text, tvb(offset, payload_len))
    pinfo.cols.info:append(" \"" .. text:gsub("\n", "\\n") .. "\"")
end

-- Main dissector function
function cwnet_proto.dissector(tvb, pinfo, tree)
    local length = tvb:len()
    if length == 0 then return end

    pinfo.cols.protocol = cwnet_proto.name

    local offset = 0
    local frame_num = 0

    while offset < length do
        frame_num = frame_num + 1

        -- Need at least 1 byte for command
        if offset + 1 > length then
            tree:add_proto_expert_info(ef.incomplete)
            return offset
        end

        local cmd_byte = tvb(offset, 1):uint()
        local cmd_type = bit.band(cmd_byte, 0x3F)
        local block_type = bit.rshift(bit.band(cmd_byte, 0xC0), 6)

        local cmd_name = cmd_types[cmd_type] or string.format("UNKNOWN(0x%02X)", cmd_type)

        -- Determine payload length
        local header_len = 1
        local payload_len = 0

        if block_type == 0 then
            -- No payload
            payload_len = 0
        elseif block_type == 1 then
            -- Short block: 1 byte length
            if offset + 2 > length then
                tree:add_proto_expert_info(ef.incomplete)
                return offset
            end
            payload_len = tvb(offset + 1, 1):uint()
            header_len = 2
        elseif block_type == 2 then
            -- Long block: 2 byte length (LE)
            if offset + 3 > length then
                tree:add_proto_expert_info(ef.incomplete)
                return offset
            end
            payload_len = tvb(offset + 1, 2):le_uint()
            header_len = 3
        else
            -- Reserved
            tree:add_proto_expert_info(ef.unknown_cmd)
            return offset
        end

        local frame_len = header_len + payload_len

        -- Check if we have complete frame
        if offset + frame_len > length then
            tree:add_proto_expert_info(ef.incomplete)
            return offset
        end

        -- Create subtree for this frame
        local subtree = tree:add(cwnet_proto, tvb(offset, frame_len),
            "CWNet " .. cmd_name .. " (" .. frame_len .. " bytes)")

        subtree:add(pf.cmd_byte, tvb(offset, 1))
        subtree:add(pf.cmd_type, tvb(offset, 1))
        subtree:add(pf.block_type, tvb(offset, 1))

        if header_len > 1 then
            subtree:add(pf.payload_len, tvb(offset + 1, header_len - 1), payload_len)
        end

        -- Set info column for first frame
        if frame_num == 1 then
            pinfo.cols.info = cmd_name
        else
            pinfo.cols.info:append(", " .. cmd_name)
        end

        -- Dissect payload based on command type
        local payload_offset = offset + header_len

        if payload_len > 0 then
            if cmd_type == 0x01 then
                dissect_connect(tvb, pinfo, subtree, payload_offset, payload_len)
            elseif cmd_type == 0x03 then
                dissect_ping(tvb, pinfo, subtree, payload_offset, payload_len)
            elseif cmd_type == 0x10 then
                dissect_morse(tvb, pinfo, subtree, payload_offset, payload_len)
            elseif cmd_type == 0x11 then
                dissect_audio(tvb, pinfo, subtree, payload_offset, payload_len)
            elseif cmd_type == 0x04 then
                dissect_print(tvb, pinfo, subtree, payload_offset, payload_len)
            else
                subtree:add(pf.payload, tvb(payload_offset, payload_len))
            end
        end

        offset = offset + frame_len
    end

    return offset
end

-- Register for TCP port 7355
local tcp_table = DissectorTable.get("tcp.port")
tcp_table:add(7355, cwnet_proto)

-- Also allow manual decode-as
register_postdissector(cwnet_proto)

print("CWNet dissector loaded - port 7355")