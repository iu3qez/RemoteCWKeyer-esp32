# CWNet Wireshark Dissector

Lua dissector for debugging CWNet protocol traffic.

## Installation

### Linux
```bash
mkdir -p ~/.local/lib/wireshark/plugins/
cp cwnet.lua ~/.local/lib/wireshark/plugins/
```

### Windows
```powershell
Copy-Item cwnet.lua "$env:APPDATA\Wireshark\plugins\"
```

### macOS
```bash
mkdir -p ~/.config/wireshark/plugins/
cp cwnet.lua ~/.config/wireshark/plugins/
```

After copying, restart Wireshark or use **Analyze → Reload Lua Plugins**.

## Usage

### Capture Filter
```
tcp port 7355
```

### Display Filters
```
cwnet                           # All CWNet traffic
cwnet.cmd_type == 0x10          # MORSE frames only
cwnet.cmd_type == 0x03          # PING frames only
cwnet.ping.type == 0            # PING requests (sync points!)
cwnet.ping.rtt > 100            # High latency pings
cwnet.morse.key == 1            # Key-down events
cwnet.perm.transmit == 1        # Connections with TX permission
```

## Decoded Fields

### Frame Header
- `cwnet.cmd_byte` - Raw command byte
- `cwnet.cmd_type` - Command type (CONNECT, PING, MORSE, etc.)
- `cwnet.block_type` - Payload length encoding
- `cwnet.payload_len` - Payload length in bytes

### CONNECT (0x01)
- `cwnet.username` - Username string
- `cwnet.callsign` - Callsign string (should be lowercase!)
- `cwnet.permissions` - Permission flags
- `cwnet.perm.talk` - Talk permission
- `cwnet.perm.transmit` - Transmit permission (required for MORSE)
- `cwnet.perm.ctrl_rig` - Rig control permission
- `cwnet.perm.admin` - Admin permission

### PING (0x03)
- `cwnet.ping.type` - REQUEST(0), RESPONSE_1(1), RESPONSE_2(2)
- `cwnet.ping.id` - Sequence ID
- `cwnet.ping.t0` - Requester's timestamp (ms)
- `cwnet.ping.t1` - Responder 1's timestamp (ms)
- `cwnet.ping.t2` - Responder 2's timestamp (ms)
- `cwnet.ping.rtt` - Calculated round-trip time (ms) [generated]

### MORSE (0x10)
- `cwnet.morse.byte` - Raw morse byte
- `cwnet.morse.key` - Key state (DOWN/UP)
- `cwnet.morse.time_enc` - Encoded 7-bit timestamp
- `cwnet.morse.time_ms` - Decoded time in milliseconds [generated]

### AUDIO (0x11)
- `cwnet.audio.samples` - Number of A-Law samples
- `cwnet.audio.duration_ms` - Duration in milliseconds [generated]

### PRINT (0x04)
- `cwnet.print.text` - Text message content

## Expert Info

The dissector provides expert info for debugging:

- **SYNC POINT** (Chat) - PING REQUEST received, client should sync timer
- **Incomplete frame** (Warning) - TCP segment doesn't contain complete frame
- **Unknown command** (Note) - Unrecognized command type

## Debugging Timer Sync Issues

1. Filter for PING requests: `cwnet.ping.type == 0`
2. Look for `[SYNC POINT]` markers in info column
3. Verify t0 values are being tracked by client
4. Check RTT stability over time (should be stable, not drifting)

## Comparing Our Client vs Official Client

```bash
# Capture our client
tshark -i eth0 -f "tcp port 7355" -w our_client.pcap

# Capture official client (same scenario)
tshark -i eth0 -f "tcp port 7355" -w official_client.pcap

# Compare with Wireshark:
# 1. Open both captures
# 2. Filter: cwnet.cmd_type == 0x03
# 3. Compare t0/t1/t2 values and RTT over time
```

## Example Output

```
Frame 1: CONNECT [iu3qez] perm=TRANSMIT+TALK
Frame 2: PING REQUEST id=1 t0=12345678 [SYNC POINT]
Frame 3: PING RESPONSE_1 id=1
Frame 4: PING RESPONSE_2 id=1 RTT=42ms
Frame 5: MORSE 5 events, 156ms total [v0,^20,v15,^40,v31]
```

Legend for MORSE events:
- `v` = key down (carrier ON)
- `^` = key up (carrier OFF)
- Number = milliseconds to wait before applying state