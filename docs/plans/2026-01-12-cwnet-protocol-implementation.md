# CWNet Protocol Implementation Guide

**Date:** 2026-01-12
**Version:** 1.0
**Based on:** Analysis of DL4YHF's official code (CwNet.c, CwStreamEnc.c, Timers.c)

---

## Table of Contents

1. [Overview](#1-overview)
   - 1.4 [Client Configuration](#14-client-configuration)
   - 1.5 [Configuration Validation](#15-configuration-validation)
   - 1.6 [Mapping to parameters.yaml](#16-mapping-to-parametersyaml)
   - 1.7 [Console Commands](#17-console-commands)
2. [Frame Format](#2-frame-format)
3. [Timer Synchronization (CRITICAL)](#3-timer-synchronization-critical)
4. [Commands](#4-commands)
5. [CW Stream Encoding](#5-cw-stream-encoding)
6. [State Machines](#6-state-machines)
7. [Integration with KeyingStream](#7-integration-with-keyingstream)
8. [PTT Management (CRITICAL)](#8-ptt-management-critical)
9. [Lessons Learned](#9-lessons-learned)
10. [Implementation Checklist](#9-implementation-checklist)
11. [Test Plan](#10-test-plan)

---

## 1. Overview

### 1.1 Architecture

```
┌─────────────┐                    ┌─────────────┐
│   CLIENT    │◄──────TCP──────────►│   SERVER    │
│  (ESP32)    │      Port 7355     │  (DL4YHF)   │
│             │                     │             │
│ - Paddle    │  MORSE frames ───► │ - Radio TX  │
│ - Sidetone  │ ◄─── AUDIO frames  │ - Spectrum  │
│             │ ◄───► PING (sync)  │             │
└─────────────┘                    └─────────────┘
```

### 1.2 Constants

```c
#define CWNET_DEFAULT_PORT              7355
#define CWNET_POLLING_INTERVAL_MS       20      // Poll every 20ms
#define CWNET_ACTIVITY_TIMEOUT_MS       5000    // Disconnect after 5s of inactivity
#define CWNET_PING_INTERVAL_MS          2000    // Ping every 2s
#define CWNET_HANDSHAKE_TIMEOUT_MS      3000    // Handshake timeout 3s
#define CWNET_STREAM_SAMPLE_RATE        8000    // Audio: 8kHz
```

### 1.3 Permissions

```c
#define CWNET_PERMISSION_NONE           0x00
#define CWNET_PERMISSION_TALK           0x01    // Text chat
#define CWNET_PERMISSION_TRANSMIT       0x02    // Can send MORSE
#define CWNET_PERMISSION_CTRL_RIG       0x04    // Radio control
#define CWNET_PERMISSION_ADMIN          0x08    // Administrator
```

### 1.4 Client Configuration

```c
/**
 * CWNet Client Configuration
 *
 * WARNING: the server is CASE SENSITIVE!
 * The callsign MUST be lowercase.
 */
typedef struct {
    // Connection
    char server_host[64];           // Server hostname or IP
    uint16_t server_port;           // TCP port (default: 7355)
    bool enabled;                   // Client enabled
    bool auto_reconnect;            // Reconnect on disconnect

    // Identification (CASE SENSITIVE!)
    char callsign[16];              // MUST be lowercase! "iu3qez" not "IU3QEZ"
    char username[44];              // Username (may differ from callsign)

    // Timing
    uint32_t ptt_tail_base_ms;      // PTT tail base (typical: 200ms)
    uint32_t reconnect_delay_ms;    // Delay between attempts (typical: 5000ms)
    uint32_t handshake_timeout_ms;  // Handshake timeout (typical: 3000ms)
} cwnet_client_config_t;

// Default values
static const cwnet_client_config_t CWNET_CLIENT_CONFIG_DEFAULT = {
    .server_host = "cwnet.example.com",
    .server_port = 7355,
    .enabled = false,               // Disabled by default
    .auto_reconnect = true,
    .callsign = "",                 // REQUIRED, lowercase!
    .username = "",
    .ptt_tail_base_ms = 200,
    .reconnect_delay_ms = 5000,
    .handshake_timeout_ms = 3000,
};
```

### 1.5 Configuration Validation

```c
/**
 * Validates configuration before connecting.
 *
 * @return ESP_OK if valid, ESP_ERR_INVALID_ARG if invalid
 */
esp_err_t cwnet_config_validate(const cwnet_client_config_t *cfg) {
    // 1. Callsign required
    if (cfg->callsign[0] == '\0') {
        ESP_LOGE(TAG, "Callsign non configurato");
        return ESP_ERR_INVALID_ARG;
    }

    // 2. Callsign MUST be lowercase (server is case sensitive!)
    for (const char *p = cfg->callsign; *p; p++) {
        if (*p >= 'A' && *p <= 'Z') {
            ESP_LOGE(TAG, "Callsign DEVE essere lowercase! '%s' contiene maiuscole",
                     cfg->callsign);
            return ESP_ERR_INVALID_ARG;
        }
    }

    // 3. Server host required
    if (cfg->server_host[0] == '\0') {
        ESP_LOGE(TAG, "Server host non configurato");
        return ESP_ERR_INVALID_ARG;
    }

    // 4. Valid port
    if (cfg->server_port == 0) {
        ESP_LOGE(TAG, "Porta server invalida");
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

/**
 * Converts callsign to lowercase in-place.
 * Call BEFORE using the configuration.
 */
void cwnet_callsign_to_lower(char *callsign) {
    for (char *p = callsign; *p; p++) {
        if (*p >= 'A' && *p <= 'Z') {
            *p = *p + ('a' - 'A');
        }
    }
}
```

### 1.6 Mapping to parameters.yaml

```yaml
# In parameters.yaml - "remote" family
remote:
  cwnet_enabled:
    type: bool
    default: false
    description: "Enable CWNet remote client"

  cwnet_server:
    type: string
    max_length: 64
    default: ""
    description: "CWNet server hostname or IP"

  cwnet_port:
    type: uint16
    default: 7355
    min: 1
    max: 65535
    description: "CWNet server TCP port"

  cwnet_callsign:
    type: string
    max_length: 16
    default: ""
    description: "Callsign (lowercase only!)"
    nvs_key: "cwnet_call"

  cwnet_username:
    type: string
    max_length: 44
    default: ""
    description: "Username (if different from callsign)"
    nvs_key: "cwnet_user"

  cwnet_ptt_tail_ms:
    type: uint32
    default: 200
    min: 50
    max: 1000
    description: "PTT tail base time (ms)"

  cwnet_auto_reconnect:
    type: bool
    default: true
    description: "Auto reconnect on disconnect"
```

### 1.7 Console Commands

```
remote status              # Show connection status and configuration
remote connect             # Connect manually
remote disconnect          # Disconnect
remote set server <host>   # Set server
remote set port <port>     # Set port
remote set callsign <call> # Set callsign (auto-lowercase)
remote set enabled <0|1>   # Enable/disable
```

---

## 2. Frame Format

### 2.1 General Structure

```
┌────────────┬──────────────┬────────────────┐
│  COMMAND   │ BLOCK LENGTH │    PAYLOAD     │
│  (1 byte)  │ (0,1,2 bytes)│  (variabile)   │
└────────────┴──────────────┴────────────────┘
```

### 2.2 Command Byte Encoding

```
Bit 7-6: Block Length Indicator
┌────┬────┬──────────────────────────────────────┐
│ 7  │ 6  │ Meaning                               │
├────┼────┼──────────────────────────────────────┤
│ 0  │ 0  │ No payload (simple command)          │
│ 0  │ 1  │ Short block (1 byte length)          │
│ 1  │ 0  │ Long block (2 bytes length, LE)      │
│ 1  │ 1  │ Reserved                              │
└────┴────┴──────────────────────────────────────┘

Bit 5-0: Command Type (0x00 - 0x3F)
```

### 2.3 Masks

```c
#define CWNET_CMD_MASK_BLOCKLEN     0xC0
#define CWNET_CMD_MASK_NO_BLOCK     0x00
#define CWNET_CMD_MASK_SHORT_BLOCK  0x40
#define CWNET_CMD_MASK_LONG_BLOCK   0x80
#define CWNET_CMD_MASK_COMMAND      0x3F
```

### 2.4 Frame Parsing

```c
typedef enum {
    FRAME_CAT_NO_PAYLOAD = 0,   // bits 7-6 = 00
    FRAME_CAT_SHORT_PAYLOAD,    // bits 7-6 = 01, 1 byte length
    FRAME_CAT_LONG_PAYLOAD,     // bits 7-6 = 10, 2 bytes length (LE)
    FRAME_CAT_RESERVED          // bits 7-6 = 11
} frame_category_t;

static inline frame_category_t get_frame_category(uint8_t cmd_byte) {
    return (frame_category_t)((cmd_byte >> 6) & 0x03);
}

static inline uint8_t get_command_type(uint8_t cmd_byte) {
    return cmd_byte & CWNET_CMD_MASK_COMMAND;
}
```

---

## 3. Timer Synchronization (CRITICAL)

### 3.1 The Problem

The client and server have independent clocks that **drift** over time. Without synchronization:
- Latency calculation becomes wrong
- Timestamps in MORSE frames become misaligned
- After minutes/hours, the server "rejects" packets

### 3.2 Official Solution (from Timers.c)

```c
// Global variable for sync offset
static int64_t g_timer_offset_us = 0;

// Reads synced time in milliseconds
int32_t timer_read_synced_ms(void) {
    int64_t now_us = esp_timer_get_time();
    int64_t synced_us = now_us + g_timer_offset_us;
    // Wrap at 2^31 to avoid negatives (as in the official code)
    return (int32_t)((synced_us / 1000) % 2147483647);
}

// CRITICAL: Call on EVERY PING REQUEST received
void timer_sync_to_server(int32_t server_time_ms) {
    int32_t our_time_ms = timer_read_synced_ms();
    int32_t delta_ms = our_time_ms - server_time_ms;

    // Adjust offset (do NOT replace it!)
    g_timer_offset_us -= (int64_t)delta_ms * 1000;
}
```

### 3.3 When to Synchronize

```
SERVER sends PING REQUEST (type=0) with t0
    │
    ▼
CLIENT receives, calls timer_sync_to_server(t0)  ◄── REQUIRED!
    │
    ▼
CLIENT responds with PING RESPONSE_1 (type=1)
    │
    ▼
SERVER receives, sends PING RESPONSE_2 (type=2)
    │
    ▼
CLIENT computes latency = t2 - t0
```

### 3.4 Error to Avoid

```c
// WRONG - offset computed only once
void on_first_ping(int32_t server_t0) {
    static bool synced = false;
    if (!synced) {
        g_offset = server_t0 - our_time;  // Never updated again!
        synced = true;
    }
}

// CORRECT - offset updated on every ping
void on_ping_request(int32_t server_t0) {
    timer_sync_to_server(server_t0);  // Always!
}
```

---

## 4. Commands

### 4.1 Command Table

| Cmd | Hex | Name | Dir | Payload | Notes |
|-----|-----|------|-----|---------|------|
| 0x01 | CONNECT | C↔S | 92 bytes | Handshake |
| 0x02 | DISCONNECT | C↔S | 0 | Graceful close |
| 0x03 | PING | C↔S | 16 bytes | Latency + sync |
| 0x04 | PRINT | S→C | variable | Text messages |
| 0x05 | TX_INFO | S→C | variable | Who is transmitting |
| 0x06 | RIGCTLD | C↔S | variable | Hamlib commands |
| 0x10 | MORSE | C→S | variable | CW keying stream |
| 0x11 | AUDIO | S→C | variable | A-Law 8kHz |
| 0x12 | VORBIS | S→C | variable | Ogg/Vorbis |
| 0x14 | CI_V | C↔S | variable | Icom CI-V |
| 0x15 | SPECTRUM | S→C | variable | Waterfall data |
| 0x16 | FREQ_REPORT | S→C | variable | VFO state |
| 0x20 | METER_REPORT | S→C | variable | S-meter, SWR |
| 0x21 | POTI_REPORT | S→C | variable | Settings |

### 4.2 CONNECT (0x01)

**Payload: 92 bytes**

```c
typedef struct __attribute__((packed)) {
    char username[44];      // Username (null-terminated)
    char callsign[44];      // Callsign (null-terminated, LOWERCASE!)
    uint32_t permissions;   // Little-endian
} cwnet_connect_payload_t;

_Static_assert(sizeof(cwnet_connect_payload_t) == 92, "Connect payload must be 92 bytes");
```

**IMPORTANT:** The server expects the callsign in **lowercase**!

```c
void prepare_connect_payload(cwnet_connect_payload_t *p, const char *call) {
    memset(p, 0, sizeof(*p));

    // Copy and convert to lowercase
    for (size_t i = 0; call[i] && i < 43; i++) {
        p->username[i] = (char)tolower((unsigned char)call[i]);
        p->callsign[i] = (char)tolower((unsigned char)call[i]);
    }

    p->permissions = 0;  // Client does not request specific permissions
}
```

**Handshake Sequence:**

```
CLIENT                              SERVER
   │                                   │
   ├── TCP connect() ─────────────────►│
   │                                   │
   │   [WAIT 100ms!]                   │  ◄── Server needs time!
   │                                   │
   ├── CONNECT (user/call) ───────────►│
   │                                   │
   │◄── CONNECT (permissions) ─────────┤
   │                                   │
   │◄── PRINT ("Welcome...") ──────────┤
   │                                   │
   │◄── PING REQUEST ──────────────────┤
   │                                   │
   ├── PING RESPONSE_1 ───────────────►│
   │                                   │
   │◄── PING RESPONSE_2 ───────────────┤
   │                                   │
   │   [CONNECTED]                     │
```

### 4.3 PING (0x03)

**Payload: 16 fixed bytes**

```c
typedef struct __attribute__((packed)) {
    uint8_t type;           // 0=REQUEST, 1=RESPONSE_1, 2=RESPONSE_2
    uint8_t id;              // Sequence ID
    uint8_t reserved[2];    // Alignment
    int32_t t0_ms;          // Timestamp requester (LE)
    int32_t t1_ms;          // Timestamp responder 1 (LE)
    int32_t t2_ms;          // Timestamp responder 2 (LE)
} cwnet_ping_payload_t;

_Static_assert(sizeof(cwnet_ping_payload_t) == 16, "Ping payload must be 16 bytes");
```

**Handling:**

```c
void handle_ping(const cwnet_ping_payload_t *ping, int64_t rx_time_us) {
    switch (ping->type) {
        case 0:  // REQUEST from the server
            // 1. SYNC THE TIMER!
            timer_sync_to_server(ping->t0_ms);

            // 2. Prepare RESPONSE_1
            cwnet_ping_payload_t resp = {
                .type = 1,
                .id = ping->id,
                .t0_ms = ping->t0_ms,
                .t1_ms = timer_read_synced_ms(),
                .t2_ms = 0
            };
            send_ping(&resp);
            break;

        case 2:  // RESPONSE_2 from the server
            // Compute latency
            int32_t latency_ms = ping->t2_ms - ping->t0_ms;
            update_latency(latency_ms);
            break;
    }
}
```

### 4.4 MORSE (0x10)

**Payload: CW byte stream**

Each byte encodes a key-up/key-down event:

```
┌───┬───────────────────────────┐
│ 7 │   6 - 0 (7 bits)         │
├───┼───────────────────────────┤
│ K │     TIMESTAMP             │
└───┴───────────────────────────┘

K (bit 7): 1 = Key DOWN, 0 = Key UP
TIMESTAMP: milliseconds to wait BEFORE applying K
```

The MORSE frame contains **multiple bytes** in sequence.

### 4.5 AUDIO (0x11)

**Payload: A-Law samples (8-bit, 8kHz)**

```c
// Decode A-Law -> 16-bit PCM
int16_t alaw_decode(uint8_t alaw) {
    // Lookup table (256 entries) for efficiency
    return alaw_decode_table[alaw];
}

// Handle received audio
void handle_audio(const uint8_t *payload, size_t len) {
    for (size_t i = 0; i < len; i++) {
        int16_t sample = alaw_decode(payload[i]);
        audio_buffer_push(sample);
    }
}
```

---

## 5. CW Stream Encoding

### 5.1 Non-Linear Timestamp (7-bit)

| Range Encoded | Range ms | Resolution | Encode Formula |
|---------------|----------|-------------|----------------|
| 0x00 - 0x1F | 0-31 | 1 ms | `t` |
| 0x20 - 0x3F | 32-156 | 4 ms | `0x20 + (t-32)/4` |
| 0x40 - 0x7F | 157-1165 | 16 ms | `0x40 + (t-157)/16` |

### 5.2 Encoding/Decoding Functions

```c
// Encode: milliseconds -> 7-bit timestamp
uint8_t cwstream_encode_timestamp(int ms) {
    if (ms < 0)    return 0x00;
    if (ms <= 31)  return (uint8_t)ms;
    if (ms <= 156) return (uint8_t)(0x20 + (ms - 32) / 4);
    if (ms <= 1165) return (uint8_t)(0x40 + (ms - 157) / 16);
    return 0x7F;  // Max encodable
}

// Decode: 7-bit timestamp -> milliseconds
int cwstream_decode_timestamp(uint8_t ts) {
    ts &= 0x7F;  // Remove key bit
    if (ts <= 0x1F) return (int)ts;
    if (ts <= 0x3F) return 32 + 4 * (int)(ts - 0x20);
    return 157 + 16 * (int)(ts - 0x40);
}
```

### 5.3 Encode Keying Event

```c
// Encode a key up/down event into the TX buffer
int cwstream_encode_event(uint8_t *buf, size_t buf_size,
                          bool key_down, int delta_ms) {
    size_t written = 0;

    // If delta > 1165ms, more bytes are needed
    while (delta_ms > 0 && written < buf_size) {
        int chunk_ms = (delta_ms > 1165) ? 1165 : delta_ms;

        uint8_t byte = cwstream_encode_timestamp(chunk_ms);
        if (key_down) {
            byte |= 0x80;
        }

        buf[written++] = byte;
        delta_ms -= chunk_ms;
    }

    return (int)written;
}
```

### 5.4 End-Of-Transmission

Two consecutive bytes with bit 7 = 0 (both key UP) indicate end of transmission:

```c
bool cwstream_is_eot(uint8_t prev_byte, uint8_t curr_byte) {
    return ((prev_byte & 0x80) == 0) && ((curr_byte & 0x80) == 0);
}
```

---

## 6. State Machines

### 6.1 Client State Machine

```
                    Start()
                       │
                       ▼
┌──────┐  DNS Query  ┌───────────┐  TCP Connect  ┌────────────┐
│ IDLE │────────────►│ RESOLVING │──────────────►│ CONNECTING │
└──────┘             └───────────┘               └────────────┘
    ▲                     │                            │
    │                     │ Error                      │ Connected
    │                     ▼                            ▼
    │                ┌───────┐                  ┌───────────┐
    │                │ ERROR │◄─────────────────│ HANDSHAKE │
    │                └───────┘  Timeout/Error   └───────────┘
    │                     │                            │
    │  Stop() or          │                            │ CONNECT ACK
    │  No reconnect       │                            ▼
    │◄────────────────────┘                     ┌───────────┐
    │                                           │ CONNECTED │
    │                                           └───────────┘
    │                       Disconnect/Error          │
    └─────────────────────────────────────────────────┘
```

### 6.2 Server State Machine

```
                    Start()
                       │
                       ▼
┌──────┐  Listen()   ┌───────────┐  Accept()    ┌───────────┐
│ IDLE │────────────►│ LISTENING │─────────────►│ HANDSHAKE │
└──────┘             └───────────┘              └───────────┘
    ▲                     │                           │
    │                     │ Error                     │ CONNECT frame
    │                     ▼                           ▼
    │                ┌───────┐                  ┌───────────┐
    │                │ ERROR │                  │ CONNECTED │
    │                └───────┘                  └───────────┘
    │                     │                           │
    │  Stop()             │                           │
    │◄────────────────────┴───────────────────────────┘
                          Client disconnect/timeout
```

### 6.3 States

```c
typedef enum {
    CWNET_STATE_IDLE = 0,
    CWNET_STATE_RESOLVING,      // Client only
    CWNET_STATE_CONNECTING,     // Client only
    CWNET_STATE_LISTENING,      // Server only
    CWNET_STATE_HANDSHAKE,
    CWNET_STATE_CONNECTED,
    CWNET_STATE_ERROR
} cwnet_state_t;
```

---

## 7. Integration with KeyingStream

### 7.1 Architectural Constraints (from ARCHITECTURE.md)

| Rule | Description |
|--------|-------------|
| 2.3.1 | NO callbacks between components |
| 2.3.2 | NO dependency injection (`SetXxx()`) |
| 2.3.3 | NO shared state beyond the stream |
| 2.3.5 | NO queues/message passing between components |

### 7.2 Solution: Remote as Producer/Consumer

```
LOCAL KEYING (Core 0 RT):
  Paddle GPIO ──► Iambic FSM ──► KeyingStream ──► Audio Consumer
                                      │
                                      ▼
                              Remote TX Consumer (Core 1)
                                      │
                                      ▼ TCP
                              ┌──────────────┐
                              │ CWNet Server │
                              └──────────────┘

REMOTE KEYING (from network):
                              ┌──────────────┐
                              │ CWNet Server │
                              └──────────────┘
                                      │ TCP
                                      ▼
                              Remote RX Producer (Core 1)
                                      │
                                      ▼
                                KeyingStream ──► Audio Consumer
                                             ──► TX HAL Consumer
```

### 7.3 Remote TX Consumer (sends local keying to remote server)

```c
// Runs on Core 1 as a Best-Effort consumer
typedef struct {
    size_t read_idx;                    // Stream read index
    int socket_fd;                      // TCP socket
    cwnet_state_t state;                // State machine
    int64_t last_key_timestamp_us;      // For delta computation
    // ... other fields
} remote_tx_consumer_t;

void remote_tx_consumer_tick(remote_tx_consumer_t *ctx,
                             const keying_stream_t *stream,
                             int64_t now_us) {
    // 1. Read new samples from the stream
    size_t write_idx = atomic_load(&stream->write_idx);

    while (ctx->read_idx != write_idx) {
        keying_sample_t sample = stream->samples[ctx->read_idx % STREAM_SIZE];
        ctx->read_idx++;

        // 2. Encode as a MORSE frame
        int delta_ms = (sample.timestamp_us - ctx->last_key_timestamp_us) / 1000;
        uint8_t morse_byte = cwstream_encode_timestamp(delta_ms);
        if (sample.key_down) {
            morse_byte |= 0x80;
        }

        // 3. Buffer for sending
        tx_buffer_push(ctx, morse_byte);
        ctx->last_key_timestamp_us = sample.timestamp_us;
    }

    // 4. Flush the buffer if needed
    if (tx_buffer_should_flush(ctx)) {
        send_morse_frame(ctx);
    }
}
```

### 7.4 Remote RX Producer (receives keying from remote server)

```c
// Runs on Core 1, writes to the stream as a Producer
typedef struct {
    int socket_fd;
    cwnet_state_t state;
    int64_t base_timestamp_us;          // Base for timestamp reconstruction
    // ... other fields
} remote_rx_producer_t;

void remote_rx_producer_on_morse(remote_rx_producer_t *ctx,
                                  keying_stream_t *stream,
                                  const uint8_t *payload,
                                  size_t len,
                                  int64_t rx_time_us) {
    for (size_t i = 0; i < len; i++) {
        uint8_t byte = payload[i];
        bool key_down = (byte & 0x80) != 0;
        int wait_ms = cwstream_decode_timestamp(byte & 0x7F);

        // Reconstruct absolute timestamp
        ctx->base_timestamp_us += (int64_t)wait_ms * 1000;

        // Write to the stream (Producer)
        keying_sample_t sample = {
            .timestamp_us = ctx->base_timestamp_us,
            .key_down = key_down,
            .source = KEYING_SOURCE_REMOTE
        };

        size_t idx = atomic_fetch_add(&stream->write_idx, 1);
        stream->samples[idx % STREAM_SIZE] = sample;
    }
}
```

---

## 8. PTT Management (CRITICAL)

### 8.1 Fundamental Principle

```
┌─────────────────────────────────────────────────────────────────┐
│  PTT MUST BE ACTIVE BEFORE THE FIRST KEY-DOWN ARRIVES            │
│  AND MUST STAY ACTIVE UNTIL (last_key + tail + latency)          │
└─────────────────────────────────────────────────────────────────┘
```

If PTT deactivates too early → **last letters truncated**
If PTT activates too late → **first letters lost**

### 8.2 PTT Timing

```
        PTT ON                                      PTT OFF
           │                                           │
           ▼                                           ▼
    ───────┬───────────────────────────────────────────┬───────
           │  CW Keying Events                         │
           │  ▄▄▄  ▄  ▄▄▄  ▄▄▄    ▄  ▄▄▄  ▄           │
           │  C    Q   C    Q      D   E              │
           │                       │                   │
           │                       │← last_key_time   │
           │                       │                   │
           │                       │←─── tail_ms ────►│
           │                       │←─── + latency ──►│
```

### 8.3 Dynamic PTT Tail

The PTT tail MUST include the measured network latency:

```c
// WRONG - fixed tail
#define PTT_TAIL_MS 200

// CORRECT - dynamic tail
uint32_t get_ptt_tail_ms(remote_ctx_t *ctx) {
    return ctx->config.ptt_tail_base_ms + ctx->measured_latency_ms;
}

// Example:
// - Base tail: 200ms
// - Measured latency: 85ms
// - Effective PTT tail: 285ms
```

### 8.4 PTT State Machine (Client TX Side)

```c
typedef enum {
    PTT_STATE_IDLE,         // No activity
    PTT_STATE_ACTIVE,       // PTT active, keying in progress
    PTT_STATE_TAIL_WAIT     // Last key-up, waiting for tail timeout
} ptt_state_t;

typedef struct {
    ptt_state_t state;
    int64_t last_key_activity_us;   // Timestamp of last key event
    uint32_t tail_ms;               // Dynamic tail (base + latency)
    bool ptt_output;                // Physical PTT output state
} ptt_controller_t;

void ptt_on_key_event(ptt_controller_t *ptt, bool key_down, int64_t now_us) {
    ptt->last_key_activity_us = now_us;

    switch (ptt->state) {
        case PTT_STATE_IDLE:
            if (key_down) {
                // First activity - activate PTT IMMEDIATELY
                ptt->ptt_output = true;
                ptt->state = PTT_STATE_ACTIVE;
                send_rigctld_command("set_ptt 1");  // Optional
            }
            break;

        case PTT_STATE_ACTIVE:
        case PTT_STATE_TAIL_WAIT:
            if (key_down) {
                // New activity - stay in ACTIVE
                ptt->state = PTT_STATE_ACTIVE;
            } else {
                // Key-up - start tail countdown
                ptt->state = PTT_STATE_TAIL_WAIT;
            }
            break;
    }
}

void ptt_tick(ptt_controller_t *ptt, int64_t now_us, uint32_t latency_ms) {
    if (ptt->state == PTT_STATE_TAIL_WAIT) {
        // Compute dynamic tail
        uint32_t dynamic_tail_us = (ptt->tail_ms + latency_ms) * 1000;

        if (now_us - ptt->last_key_activity_us >= dynamic_tail_us) {
            // Tail timeout expired - deactivate PTT
            ptt->ptt_output = false;
            ptt->state = PTT_STATE_IDLE;
            send_rigctld_command("set_ptt 0");  // Optional
        }
    }
}
```

### 8.5 PTT via rigctld (Optional)

The CWNet protocol supports rigctld commands for explicit PTT control:

```c
// Client → Server
void send_ptt_command(bool ptt_on) {
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "set_ptt %d\n", ptt_on ? 1 : 0);
    send_rigctld_frame(cmd);
}

// Frame format:
// 0x46 [length] "set_ptt 1\n\0"
// │              └─ ASCII command + newline + null
// └─ 0x06 | 0x40 = CMD_RIGCTLD with short block
```

**Note:** the `set_ptt` command is **optional**. The server can infer PTT from the presence of MORSE frames. However, the explicit command reduces TX activation latency.

### 8.6 PTT Server Side (Keying Reception)

When the server receives MORSE frames from a remote client:

```c
// From CwNet.c:2885-2900
void server_on_morse_received(server_ctx_t *ctx, int client_idx, uint8_t morse_byte) {
    // 1. Check permissions
    if (!(ctx->clients[client_idx].permissions & CWNET_PERMISSION_TRANSMIT)) {
        return;  // Silently ignore!
    }

    // 2. Handle "who has the key"
    if (ctx->transmitting_client < 0) {
        // No one is transmitting - give the key to this client
        ctx->transmitting_client = client_idx;
    }

    if (ctx->transmitting_client != client_idx) {
        // Another client has the key - ignore (or break-in after timeout)
        return;
    }

    // 3. Insert into the FIFO for the keyer thread
    ctx->morse_rx_fifo[ctx->fifo_head].byte = morse_byte;
    ctx->morse_rx_fifo[ctx->fifo_head].rx_time_ms = timer_read_synced_ms();
    ctx->fifo_head = (ctx->fifo_head + 1) % FIFO_SIZE;

    // 4. PTT is handled by the keyer thread based on the FIFO
}
```

### 8.7 Common PTT Problems

| Problem | Cause | Solution |
|----------|-------|-----------|
| First letters cut off | PTT activated after key-down | Activate PTT on the FIRST key-down |
| Last letters cut off | Tail too short | Tail = base + measured latency |
| PTT does not deactivate | No timeout | Implement a tail timeout |
| PTT cycles on/off | Incorrect `last_key_activity` reset | Reset only after PTT OFF |
| High latency → truncation | Fixed tail not sufficient | DYNAMIC tail required |

### 8.8 PTT Diagnostics

```c
// Log PTT state transitions for debugging
void log_ptt_transition(ptt_state_t old_state, ptt_state_t new_state,
                        bool ptt_output, int64_t now_us) {
    ESP_LOGI(TAG, "PTT: %s → %s, output=%s, time=%lld",
             ptt_state_str(old_state),
             ptt_state_str(new_state),
             ptt_output ? "ON" : "OFF",
             now_us / 1000);
}

// Verify tail timing
void verify_ptt_tail(int64_t last_key_us, int64_t ptt_off_us,
                     uint32_t expected_tail_ms) {
    int64_t actual_tail_us = ptt_off_us - last_key_us;
    int64_t expected_tail_us = expected_tail_ms * 1000;
    int64_t error_us = actual_tail_us - expected_tail_us;

    if (abs(error_us) > 10000) {  // > 10ms error
        ESP_LOGW(TAG, "PTT tail error: expected=%ldms, actual=%ldms, error=%ldms",
                 expected_tail_ms,
                 (long)(actual_tail_us / 1000),
                 (long)(error_us / 1000));
    }
}
```

---

## 9. Lessons Learned

### 8.1 Critical Bugs from the C++ Implementation

| Bug | Cause | Fix |
|-----|-------|-----|
| Latency drift | Timer offset computed once | Call `timer_sync_to_server()` on EVERY ping request |
| Duplicate CONNECT | Checking `tx_head == tx_tail` for sent | Use a boolean flag `connect_sent` |
| Handshake fail | CONNECT sent too early | Wait 100ms after TCP connect |
| Callsign rejected | Case sensitivity | Convert to lowercase |
| Buffer overflow | Large audio frames | Buffer minimum 8KB |

### 8.2 Critical Timing

| Operation | Timing | Notes |
|------------|--------|------|
| Post-connect delay | 100ms | DL4YHF server needs time |
| Handshake timeout | 3000ms | Abort if no ACK |
| Ping interval | 2000ms | For sync + latency |
| Activity timeout | 5000ms | Server disconnects |
| Poll interval | 20ms | select() timeout |

### 8.3 Patterns to Follow

```c
// 1. Activity watchdog - send SOMETHING every <5s
void feed_activity_watchdog(ctx) {
    if (time_since_last_tx() > 4000) {
        send_ping_request();  // Keep-alive
    }
}

// 2. Non-blocking socket with select()
struct timeval tv = { .tv_sec = 0, .tv_usec = 50000 };  // 50ms
int ready = select(fd + 1, &readfds, &writefds, NULL, &tv);

// 3. Streaming parser for fragmented frames
while (rx_buffer_has_complete_frame()) {
    frame_t frame = parse_frame();
    handle_frame(&frame);
}
```

---

## 9. Implementation Checklist

### 9.1 Core Protocol

- [ ] Frame parser (streaming, handles fragmentation)
- [ ] Frame builder (with correct length encoding)
- [ ] Timer sync (`timer_sync_to_server()` on every ping)
- [ ] CW stream encoder/decoder (7-bit timestamp)
- [ ] A-Law codec (for audio)

### 9.2 Client

- [ ] DNS resolution (non-blocking)
- [ ] TCP connect (non-blocking with select)
- [ ] State machine (IDLE→RESOLVING→CONNECTING→HANDSHAKE→CONNECTED)
- [ ] Handshake: wait 100ms, send CONNECT, wait ACK
- [ ] Ping handling: SYNC timer on request, calc latency on response_2
- [ ] MORSE TX: encode keying events, batch in frames
- [ ] AUDIO RX: decode A-Law, push to audio buffer
- [ ] Activity watchdog: send ping if idle >4s
- [ ] Auto-reconnect on error

### 9.3 Server

- [ ] TCP listen (non-blocking)
- [ ] Accept connection (single client for now)
- [ ] State machine (IDLE→LISTENING→HANDSHAKE→CONNECTED)
- [ ] CONNECT handling: validate, send ACK with permissions
- [ ] Ping initiation: send request periodically
- [ ] MORSE RX: decode, push to KeyingStream
- [ ] Permission check: CWNET_PERMISSION_TRANSMIT for MORSE
- [ ] Activity timeout: disconnect after 5s of inactivity

### 9.4 Integration

- [ ] Remote TX Consumer (reads KeyingStream, sends MORSE)
- [ ] Remote RX Producer (receives MORSE, writes KeyingStream)
- [ ] No callbacks (polling-based)
- [ ] No queues between components
- [ ] Core 1 only (Best-Effort, not RT)

---

## 10. Test Plan

### 10.0 Wireshark Dissector

A Lua dissector is available at `tools/wireshark/cwnet.lua` for debugging traffic.

**Installation:**
```bash
# Linux
cp tools/wireshark/cwnet.lua ~/.local/lib/wireshark/plugins/

# Restart Wireshark or Analyze → Reload Lua Plugins
```

**Useful filters:**
```
cwnet                       # All CWNet traffic
cwnet.ping.type == 0        # PING REQUEST (sync points!)
cwnet.ping.rtt > 100        # High latency
cwnet.morse.key == 1        # Key-down events
cwnet.cmd_type == 0x10      # MORSE frames only
```

**Expert Info:**
- `[SYNC POINT]` - indicates where the client must call `timer_sync_to_server()`
- RTT automatically computed on PING RESPONSE_2

See [tools/wireshark/README.md](../../tools/wireshark/README.md) for details.

### 10.1 Unit Tests (Host)

| Test | Description |
|------|-------------|
| `test_frame_parse` | Parse complete and fragmented frames |
| `test_timestamp_encode` | Verify 7-bit encoding |
| `test_timestamp_decode` | Verify 7-bit decoding |
| `test_timer_sync` | Verify sync does not drift |
| `test_alaw_codec` | Encode/decode round-trip |

### 10.2 Integration Tests (with DL4YHF Server)

| Test | Procedure |
|------|-----------|
| Handshake | Connect, verify ACK with permissions |
| Latency | 10 ping cycles, verify stability |
| Long session | connected for 1 hour, verify no drift |
| MORSE TX | Send CQ, verify on server |
| AUDIO RX | Verify reception and playback |
| Reconnect | Kill connection, verify auto-reconnect |

### 10.3 Wireshark Comparison

```bash
# Capture our client's traffic
wireshark -i eth0 -f "tcp port 7355" -w our_client.pcap

# Capture the official client's traffic (same scenario)
wireshark -i eth0 -f "tcp port 7355" -w official_client.pcap

# Compare byte-by-byte:
# - CONNECT frame structure
# - PING timestamps (t0, t1, t2)
# - MORSE frame encoding
```

---

## Appendix A: Source File References

| File | Contents |
|------|-----------|
| `tmp/CwNet.c` | Official server/client (4177 lines) |
| `tmp/CwStreamEnc.c` | CW stream encoding (230 lines) |
| `tmp/Timers.c` | Timer sync (310 lines) |
| `tmp/remote_cw_client.cpp` | Our C++ implementation (bug reference) |
| `tmp/docs/RemoteCwNetProtocol.md` | Protocol documentation |

---

## Appendix B: Quick Reference

### Frame Examples

```
CONNECT (short block):
41 5C [92 bytes payload]
│  │   └─ username[44] + callsign[44] + permissions[4]
│  └─ Length = 92
└─ 0x01 | 0x40 = CMD_CONNECT with short block

PING (short block):
43 10 [16 bytes payload]
│  │   └─ type + id + reserved[2] + t0[4] + t1[4] + t2[4]
│  └─ Length = 16
└─ 0x03 | 0x40 = CMD_PING with short block

MORSE (short block):
50 05 80 14 8F 22 9F
│  │  └──┴──┴──┴──┴─ 5 CW bytes
│  └─ Length = 5
└─ 0x10 | 0x40 = CMD_MORSE with short block

AUDIO (long block):
91 40 01 [320 bytes A-Law samples]
│  │  │   └─ 40ms @ 8kHz
│  └──┴─ Length = 320 (little-endian)
└─ 0x11 | 0x80 = CMD_AUDIO with long block
```

---

**End of Document**
