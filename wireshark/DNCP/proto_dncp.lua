-- Create a new dissector
local dncp_proto = Proto("dncp", "Dynamic Node Configuration Protocol")

-- Helper function to format a 6-byte ByteArray as a MAC address string
local function format_mac(mac_bytes)
    return string.format(
        "%02x:%02x:%02x:%02x:%02x:%02x",
        mac_bytes:get_index(0),
        mac_bytes:get_index(1),
        mac_bytes:get_index(2),
        mac_bytes:get_index(3),
        mac_bytes:get_index(4),
        mac_bytes:get_index(5)
    )
end

-- Define the fields for the DNCP protocol
local fields = dncp_proto.fields
fields.length = ProtoField.uint16("dncp.length", "Message Length", base.DEC)
fields.mac_address = ProtoField.string("dncp.mac_address", "MAC Address")
fields.protocol_version = ProtoField.uint8("dncp.protocol_version", "Protocol Version", base.DEC)
fields.message_type = ProtoField.uint8(
  "dncp.message_type", "Message Type", base.DEC,
  {
    [0]="REQUEST",
    [1]="REQUEST_NO_RESPONSE",
    [2]="RESPONSE",
    [3]="ERROR",
    [4]="NOTIFICATION"
  })
fields.message_id = ProtoField.uint16(
  "dncp.message_id", "Message ID", base.DEC,
  {
    [0x100] = "Registry",
    [0x200] = "Announce",
    [0x300] = "StartTDMeasurement",
    [0x301] = "GetTDMeasurementResult",
    [0x400] = "StoreSettings",
    [0x401] = "Activate"
  })
fields.message_cnt = ProtoField.uint16("dncp.message_cnt", "Message Counter", base.DEC)

-- Fields specific to Registry
fields.registry_enum_channel = ProtoField.uint8("dncp.registry_enum_channel", "Enumeration Channel", base.DEC)
fields.registry_node_count = ProtoField.uint8("dncp.registry_node_count", "Node Count", base.DEC)
fields.num_of_entries = ProtoField.uint8("dncp.num_of_entries", "Number of Entries", base.DEC)
fields.entry_mac = ProtoField.string("dncp.entry_mac", "Entry MAC Address")
fields.entry_device_id = ProtoField.uint64("dncp.entry_device_id", "Entry Device ID", base.DEC)
fields.entry_ipv6 = ProtoField.ipv6("dncp.entry_ipv6", "Entry IPv6 Address")
fields.entry_ipv4 = ProtoField.ipv4("dncp.entry_ipv4", "Entry IPv4 Address")
fields.entry_state = ProtoField.uint8("dncp.entry_state", "Entry State", base.DEC, {[0]="Unpaired", [1]="Paired"})
fields.entry_announce_request = ProtoField.uint8("dncp.entry_announce_request", "Entry Announce Request", base.DEC, {[0]="No", [1]="Yes"})
fields.entry_burst_frames_per_to = ProtoField.uint8("dncp.entry_burst_frames_per_to", "Entry Burst Frames per TO")
fields.entry_number_plca_slots = ProtoField.uint8("dncp.entry_number_plca_slots", "Entry Number of PLCA slots")
fields.entry_plca_ids = ProtoField.string("dncp.entry_plca_ids", "Entry PLCA Node Identifiers")

-- Fields specific to Announce
fields.announce_mac = ProtoField.string("dncp.announce_mac", "MAC Address")
fields.announce_device_id = ProtoField.uint64("dncp.announce_device_id", "Device ID", base.DEC)
fields.announce_ipv6 = ProtoField.ipv6("dncp.entry_ipv6", "Entry IPv6 Address")
fields.announce_ipv4 = ProtoField.ipv4("dncp.announce_ipv4", "IPv4 Address")
fields.announce_persistency = ProtoField.uint8("dncp.announce_persistency", "Persistency", base.DEC, {[0]="Non-Persistent",
                                                                                                      [1]="Persistent"})
fields.announce_state = ProtoField.uint8("dncp.announce_state", "State", base.DEC, {[0]="Undefined",
                                                                                    [1]="Unconfigured",
                                                                                    [2]="Pre-Configured",
                                                                                    [3]="Configured"})
fields.announce_burst_frames_per_to = ProtoField.uint8("dncp.announce_burst_frames_per_to", "Burst Frames per TO")
fields.announce_number_plca_slots = ProtoField.uint8("dncp.announce_number_plca_slots", "Number of PLCA slots")
fields.announce_entry_plca_id = ProtoField.string("dncp.announce_entry_plca_id", "Entry PLCA Node Identifier")


-- Fields specific to TD
fields.initiator_mac = ProtoField.string("dncp.initiator_mac", "Initiator MAC Address")
fields.reference_mac = ProtoField.string("dncp.reference_mac", "Reference MAC Address")
fields.measurement_mac = ProtoField.string("dncp.measurement_mac", "Measurement MAC Address")
fields.role = ProtoField.uint8("dncp.role", "Role", base.DEC, {[0]="Initiator", [1]="Reference", [2]="Measurement"})
fields.duration = ProtoField.uint8("dncp.duration", "Duration", base.DEC)
fields.internal_delay = ProtoField.uint32("dncp.internal_delay", "Internal Delay", base.DEC)
fields.internal_delay_measured = ProtoField.uint32("dncp.internal_delay_measured", "Internal Delay on Measured Node", base.DEC)
fields.network_delay = ProtoField.uint32("dncp.network_delay", "Network Delay", base.DEC)
fields.error_code = ProtoField.uint8("dncp.error_code", "Error Code", base.DEC, {[0]="E_UNKNOWN_METHOD", [1]="E_NOT_READY"})

-- Fields specific to activate
fields.state = ProtoField.uint8("dncp.state", "State", base.DEC, {[0]="Off", [1]="On"})

-- Dissector function
function dncp_proto.dissector(buffer, pinfo, tree)
    pinfo.cols.protocol = dncp_proto.name

    local subtree = tree:add(dncp_proto, buffer(), "DNCP Protocol Data")

    local header_tree = subtree:add(dncp_proto, buffer(), "Header")
    header_tree:add(fields.length, buffer(0, 2))
    header_tree:add(fields.mac_address, format_mac(buffer(2+2, 6):bytes()))
    header_tree:add(fields.protocol_version, buffer(10, 1))
    header_tree:add(fields.message_type, buffer(11, 1))
    header_tree:add(fields.message_id, buffer(12, 2))
    header_tree:add(fields.message_cnt, buffer(14, 2))

    local message_type = buffer(11, 1):uint()
    local message_id = buffer(12, 2):uint()

    if message_id == 0x100 then -- Registry (0x0100)
        local msg_tree = subtree:add(dncp_proto, buffer(), "Message: Registry")

        msg_tree:add(fields.registry_enum_channel, buffer(16, 1))
        msg_tree:add(fields.registry_node_count, buffer(17, 1))
        local num_of_entries = buffer(18, 1):uint()
        msg_tree:add(fields.num_of_entries, buffer(18, 1))

        local offset = 19
        for i = 1, num_of_entries do
            local entry_tree = msg_tree:add(dncp_proto, buffer(offset, 39), "Entry " .. i)
            entry_tree:add(fields.entry_mac, format_mac(buffer(offset+2, 6):bytes()))
            entry_tree:add(fields.entry_device_id, buffer(offset + 8, 8))
            local ipv6_address = buffer(offset + 16, 16)
            entry_tree:add(fields.entry_ipv6, ipv6_address)
            entry_tree:add(fields.entry_ipv4, buffer(offset + 32, 4))
            entry_tree:add(fields.entry_state, buffer(offset + 36, 1))
            entry_tree:add(fields.entry_announce_request, buffer(offset + 37, 1))
            entry_tree:add(fields.entry_burst_frames_per_to, buffer(offset + 38, 1))
            entry_tree:add(fields.entry_number_plca_slots, buffer(offset + 39, 1))
            local num_of_plca_slots =  buffer(offset + 39, 1):uint()
            local plca_ids = {}
            for j = 1, num_of_plca_slots do
              table.insert(plca_ids, tostring(buffer(offset + 39 + j, 1):uint()))
            end
            entry_tree:add(fields.entry_plca_ids, table.concat(plca_ids, ", "))
            offset = offset + num_of_plca_slots + 40
        end

    elseif message_id == 0x200 then -- Announce (0x0200)
        local msg_tree = subtree:add(dncp_proto, buffer(), "Message: Announce")
        msg_tree:add(fields.announce_mac, format_mac(buffer(16+2, 6):bytes()))
        msg_tree:add(fields.announce_device_id, buffer(24, 8))
        msg_tree:add(fields.announce_ipv6, buffer(32, 16))
        msg_tree:add(fields.announce_ipv4, buffer(48, 4))
        msg_tree:add(fields.announce_persistency, buffer(52, 1))

        msg_tree:add(fields.announce_state, buffer(53, 1))
        msg_tree:add(fields.announce_burst_frames_per_to, buffer(54, 1))
        msg_tree:add(fields.announce_number_plca_slots, buffer(55, 1))
        local num_of_plca_slots =  buffer(55, 1):uint()
        local plca_ids = {}
        for j = 1, num_of_plca_slots do
          table.insert(plca_ids, tostring(buffer(55 + j, 1):uint()))
        end
        msg_tree:add(fields.announce_entry_plca_id, table.concat(plca_ids, ", "))


    elseif message_id == 0x300 then -- StartTDMeasurement (0x0300)
        local msg_tree = subtree:add(dncp_proto, buffer(), "Message: StartTDMeasurement")
        if message_type == 0 then -- REQUEST
          msg_tree:add(fields.initiator_mac, format_mac(buffer(16+2, 6):bytes()))
          msg_tree:add(fields.reference_mac, format_mac(buffer(16+10, 6):bytes()))
          msg_tree:add(fields.measurement_mac, format_mac(buffer(16+18, 6):bytes()))
          msg_tree:add(fields.duration, buffer(16+24, 1))
        elseif message_type == 2 then
            -- RESPONSE
            -- No parameters for RESPONSE
        elseif message_type == 3 then
            -- ERROR
            msg_tree:add(fields.error_code, buffer(16, 1))
        end

    elseif message_id == 0x301 then -- GetTDMeasurementResult (0x0301)
      local msg_tree = subtree:add(dncp_proto, buffer(), "Message: GetTDMeasurementResult")
      if message_type == 0 then
          -- REQUEST
      elseif message_type == 2 then
          -- RESPONSE
          msg_tree:add(fields.duration, buffer(16, 1))
          msg_tree:add(fields.internal_delay, buffer(17, 4))
          msg_tree:add(fields.internal_delay_measured, buffer(21, 4))
          msg_tree:add(fields.network_delay, buffer(25, 4))
      elseif message_type == 3 then
          -- ERROR
          msg_tree:add(fields.error_code, buffer(16, 1))
      end

    elseif message_id == 0x400 then -- StoreSettings (0x0400)
      local msg_tree = subtree:add(dncp_proto, buffer(), "Message: StoreSettings")
      if message_type == 3 then
        -- ERROR
        msg_tree:add(fields.error_code, buffer(16, 1))
      end

    elseif message_id == 0x401 then -- Activate (0x0401)
      local msg_tree = subtree:add(dncp_proto, buffer(), "Message: Activate")
      if message_type == 0 then -- REQUEST
        msg_tree:add(fields.state, buffer(16, 1))
      elseif message_type == 2 then
          -- RESPONSE
          -- No parameters for RESPONSE
      elseif message_type == 3 then
          -- ERROR
          msg_tree:add(fields.error_code, buffer(16, 1))
      end
    end

    -- Set the packet info column
    local message_id_names =
    {
      [0x100] = "Registry",
      [0x200] = "Announce",
      [0x300] = "StartTDMeasurement",
      [0x301] = "GetTDMeasurementResult",
      [0x400] = "StoreSettings",
      [0x401] = "Activate"
    }
    local message_id_name = message_id_names[message_id] or "Unknown"
    local message_type_names = {[0]="REQUEST", [1]="REQUEST_NO_RESPONSE", [2]="RESPONSE", [3]="ERROR", [4]="NOTIFICATION"}
    local message_type_name = message_type_names[message_type] or "Unknown"
    pinfo.cols.info = string.format("%s [%s]", message_id_name, message_type_name)
end

-- Register the dissector
local udp_table = DissectorTable.get("udp.port")
udp_table:add(65526, dncp_proto)
udp_table:add(65527, dncp_proto)
