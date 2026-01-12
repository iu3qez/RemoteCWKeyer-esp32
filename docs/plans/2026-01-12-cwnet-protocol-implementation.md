# CWNet Protocol Implementation Guide

**Data:** 2026-01-12
**Versione:** 1.0
**Basato su:** Analisi codice ufficiale DL4YHF (CwNet.c, CwStreamEnc.c, Timers.c)

---

## Indice

1. [Overview](#1-overview)
2. [Frame Format](#2-frame-format)
3. [Timer Synchronization (CRITICO)](#3-timer-synchronization-critico)
4. [Commands](#4-commands)
5. [CW Stream Encoding](#5-cw-stream-encoding)
6. [State Machines](#6-state-machines)
7. [Integration con KeyingStream](#7-integration-con-keyingstream)
8. [Lessons Learned](#8-lessons-learned)
9. [Implementation Checklist](#9-implementation-checklist)
10. [Test Plan](#10-test-plan)

---

## 1. Overview

### 1.1 Architettura

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

### 1.2 Costanti

```c
#define CWNET_DEFAULT_PORT              7355
#define CWNET_POLLING_INTERVAL_MS       20      // Poll ogni 20ms
#define CWNET_ACTIVITY_TIMEOUT_MS       5000    // Disconnect dopo 5s inattività
#define CWNET_PING_INTERVAL_MS          2000    // Ping ogni 2s
#define CWNET_HANDSHAKE_TIMEOUT_MS      3000    // Timeout handshake 3s
#define CWNET_STREAM_SAMPLE_RATE        8000    // Audio: 8kHz
```

### 1.3 Permessi

```c
#define CWNET_PERMISSION_NONE           0x00
#define CWNET_PERMISSION_TALK           0x01    // Chat testuale
#define CWNET_PERMISSION_TRANSMIT       0x02    // Può inviare MORSE
#define CWNET_PERMISSION_CTRL_RIG       0x04    // Controllo radio
#define CWNET_PERMISSION_ADMIN          0x08    // Amministratore
```

---

## 2. Frame Format

### 2.1 Struttura Generale

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
│ 7  │ 6  │ Significato                          │
├────┼────┼──────────────────────────────────────┤
│ 0  │ 0  │ No payload (comando semplice)        │
│ 0  │ 1  │ Short block (1 byte length)          │
│ 1  │ 0  │ Long block (2 bytes length, LE)      │
│ 1  │ 1  │ Riservato                            │
└────┴────┴──────────────────────────────────────┘

Bit 5-0: Command Type (0x00 - 0x3F)
```

### 2.3 Maschere

```c
#define CWNET_CMD_MASK_BLOCKLEN     0xC0
#define CWNET_CMD_MASK_NO_BLOCK     0x00
#define CWNET_CMD_MASK_SHORT_BLOCK  0x40
#define CWNET_CMD_MASK_LONG_BLOCK   0x80
#define CWNET_CMD_MASK_COMMAND      0x3F
```

### 2.4 Parsing Frame

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

## 3. Timer Synchronization (CRITICO)

### 3.1 Il Problema

Il client e server hanno clock indipendenti che **driftano** nel tempo. Senza sincronizzazione:
- Latency calculation diventa errata
- Timestamp nei frame MORSE non allineati
- Dopo minuti/ore, il server "rifiuta" i pacchetti

### 3.2 Soluzione Ufficiale (da Timers.c)

```c
// Variabile globale per offset sincronizzazione
static int64_t g_timer_offset_us = 0;

// Legge tempo sincronizzato in millisecondi
int32_t timer_read_synced_ms(void) {
    int64_t now_us = esp_timer_get_time();
    int64_t synced_us = now_us + g_timer_offset_us;
    // Wrap a 2^31 per evitare negativi (come ufficiale)
    return (int32_t)((synced_us / 1000) % 2147483647);
}

// CRITICO: Chiamare ad OGNI ricezione di PING REQUEST
void timer_sync_to_server(int32_t server_time_ms) {
    int32_t our_time_ms = timer_read_synced_ms();
    int32_t delta_ms = our_time_ms - server_time_ms;

    // Aggiusta offset (NON sostituirlo!)
    g_timer_offset_us -= (int64_t)delta_ms * 1000;
}
```

### 3.3 Quando Sincronizzare

```
SERVER invia PING REQUEST (type=0) con t0
    │
    ▼
CLIENT riceve, chiama timer_sync_to_server(t0)  ◄── OBBLIGATORIO!
    │
    ▼
CLIENT risponde con PING RESPONSE_1 (type=1)
    │
    ▼
SERVER riceve, invia PING RESPONSE_2 (type=2)
    │
    ▼
CLIENT calcola latency = t2 - t0
```

### 3.4 Errore da Evitare

```c
// SBAGLIATO - offset calcolato una volta sola
void on_first_ping(int32_t server_t0) {
    static bool synced = false;
    if (!synced) {
        g_offset = server_t0 - our_time;  // Mai più aggiornato!
        synced = true;
    }
}

// CORRETTO - offset aggiornato ad ogni ping
void on_ping_request(int32_t server_t0) {
    timer_sync_to_server(server_t0);  // Sempre!
}
```

---

## 4. Commands

### 4.1 Tabella Comandi

| Cmd | Hex | Nome | Dir | Payload | Note |
|-----|-----|------|-----|---------|------|
| 0x01 | CONNECT | C↔S | 92 bytes | Handshake |
| 0x02 | DISCONNECT | C↔S | 0 | Chiusura graceful |
| 0x03 | PING | C↔S | 16 bytes | Latency + sync |
| 0x04 | PRINT | S→C | variabile | Messaggi testo |
| 0x05 | TX_INFO | S→C | variabile | Chi sta trasmettendo |
| 0x06 | RIGCTLD | C↔S | variabile | Comandi Hamlib |
| 0x10 | MORSE | C→S | variabile | CW keying stream |
| 0x11 | AUDIO | S→C | variabile | A-Law 8kHz |
| 0x12 | VORBIS | S→C | variabile | Ogg/Vorbis |
| 0x14 | CI_V | C↔S | variabile | Icom CI-V |
| 0x15 | SPECTRUM | S→C | variabile | Waterfall data |
| 0x16 | FREQ_REPORT | S→C | variabile | VFO state |
| 0x20 | METER_REPORT | S→C | variabile | S-meter, SWR |
| 0x21 | POTI_REPORT | S→C | variabile | Settings |

### 4.2 CONNECT (0x01)

**Payload: 92 bytes**

```c
typedef struct __attribute__((packed)) {
    char username[44];      // Nome utente (null-terminated)
    char callsign[44];      // Callsign (null-terminated, LOWERCASE!)
    uint32_t permissions;   // Little-endian
} cwnet_connect_payload_t;

_Static_assert(sizeof(cwnet_connect_payload_t) == 92, "Connect payload must be 92 bytes");
```

**IMPORTANTE:** Il server si aspetta callsign in **lowercase**!

```c
void prepare_connect_payload(cwnet_connect_payload_t *p, const char *call) {
    memset(p, 0, sizeof(*p));

    // Copia e converti in lowercase
    for (size_t i = 0; call[i] && i < 43; i++) {
        p->username[i] = (char)tolower((unsigned char)call[i]);
        p->callsign[i] = (char)tolower((unsigned char)call[i]);
    }

    p->permissions = 0;  // Client non richiede permessi specifici
}
```

**Sequenza Handshake:**

```
CLIENT                              SERVER
   │                                   │
   ├── TCP connect() ─────────────────►│
   │                                   │
   │   [ATTENDI 100ms!]                │  ◄── Server needs time!
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

**Payload: 16 bytes fissi**

```c
typedef struct __attribute__((packed)) {
    uint8_t type;           // 0=REQUEST, 1=RESPONSE_1, 2=RESPONSE_2
    uint8_t id;             // Sequence ID
    uint8_t reserved[2];    // Allineamento
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
        case 0:  // REQUEST dal server
            // 1. SINCRONIZZA IL TIMER!
            timer_sync_to_server(ping->t0_ms);

            // 2. Prepara RESPONSE_1
            cwnet_ping_payload_t resp = {
                .type = 1,
                .id = ping->id,
                .t0_ms = ping->t0_ms,
                .t1_ms = timer_read_synced_ms(),
                .t2_ms = 0
            };
            send_ping(&resp);
            break;

        case 2:  // RESPONSE_2 dal server
            // Calcola latency
            int32_t latency_ms = ping->t2_ms - ping->t0_ms;
            update_latency(latency_ms);
            break;
    }
}
```

### 4.4 MORSE (0x10)

**Payload: Stream di byte CW**

Ogni byte codifica un evento key-up/key-down:

```
┌───┬───────────────────────────┐
│ 7 │   6 - 0 (7 bits)         │
├───┼───────────────────────────┤
│ K │     TIMESTAMP             │
└───┴───────────────────────────┘

K (bit 7): 1 = Key DOWN, 0 = Key UP
TIMESTAMP: Millisecondi da attendere PRIMA di applicare K
```

Il frame MORSE contiene **multipli byte** in sequenza.

### 4.5 AUDIO (0x11)

**Payload: Campioni A-Law (8-bit, 8kHz)**

```c
// Decodifica A-Law -> PCM 16-bit
int16_t alaw_decode(uint8_t alaw) {
    // Tabella di lookup (256 entries) per efficienza
    return alaw_decode_table[alaw];
}

// Gestione audio ricevuto
void handle_audio(const uint8_t *payload, size_t len) {
    for (size_t i = 0; i < len; i++) {
        int16_t sample = alaw_decode(payload[i]);
        audio_buffer_push(sample);
    }
}
```

---

## 5. CW Stream Encoding

### 5.1 Timestamp Non-Lineare (7-bit)

| Range Encoded | Range ms | Risoluzione | Formula Encode |
|---------------|----------|-------------|----------------|
| 0x00 - 0x1F | 0-31 | 1 ms | `t` |
| 0x20 - 0x3F | 32-156 | 4 ms | `0x20 + (t-32)/4` |
| 0x40 - 0x7F | 157-1165 | 16 ms | `0x40 + (t-157)/16` |

### 5.2 Funzioni Encoding/Decoding

```c
// Encode: millisecondi -> 7-bit timestamp
uint8_t cwstream_encode_timestamp(int ms) {
    if (ms < 0)    return 0x00;
    if (ms <= 31)  return (uint8_t)ms;
    if (ms <= 156) return (uint8_t)(0x20 + (ms - 32) / 4);
    if (ms <= 1165) return (uint8_t)(0x40 + (ms - 157) / 16);
    return 0x7F;  // Max encodable
}

// Decode: 7-bit timestamp -> millisecondi
int cwstream_decode_timestamp(uint8_t ts) {
    ts &= 0x7F;  // Rimuovi bit key
    if (ts <= 0x1F) return (int)ts;
    if (ts <= 0x3F) return 32 + 4 * (int)(ts - 0x20);
    return 157 + 16 * (int)(ts - 0x40);
}
```

### 5.3 Encode Keying Event

```c
// Encode un evento key up/down nel buffer TX
int cwstream_encode_event(uint8_t *buf, size_t buf_size,
                          bool key_down, int delta_ms) {
    size_t written = 0;

    // Se delta > 1165ms, servono più byte
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

Due byte consecutivi con bit 7 = 0 (entrambi key UP) indicano fine trasmissione:

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

### 6.3 Stati

```c
typedef enum {
    CWNET_STATE_IDLE = 0,
    CWNET_STATE_RESOLVING,      // Solo client
    CWNET_STATE_CONNECTING,     // Solo client
    CWNET_STATE_LISTENING,      // Solo server
    CWNET_STATE_HANDSHAKE,
    CWNET_STATE_CONNECTED,
    CWNET_STATE_ERROR
} cwnet_state_t;
```

---

## 7. Integration con KeyingStream

### 7.1 Vincoli Architetturali (da ARCHITECTURE.md)

| Regola | Descrizione |
|--------|-------------|
| 2.3.1 | NO callbacks tra componenti |
| 2.3.2 | NO dependency injection (`SetXxx()`) |
| 2.3.3 | NO shared state oltre allo stream |
| 2.3.5 | NO queues/message passing tra componenti |

### 7.2 Soluzione: Remote come Producer/Consumer

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

REMOTE KEYING (da rete):
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

### 7.3 Remote TX Consumer (invia keying locale a server remoto)

```c
// Gira su Core 1 come Best-Effort consumer
typedef struct {
    size_t read_idx;                    // Indice lettura stream
    int socket_fd;                      // TCP socket
    cwnet_state_t state;                // State machine
    int64_t last_key_timestamp_us;      // Per calcolo delta
    // ... altri campi
} remote_tx_consumer_t;

void remote_tx_consumer_tick(remote_tx_consumer_t *ctx,
                             const keying_stream_t *stream,
                             int64_t now_us) {
    // 1. Leggi nuovi sample dallo stream
    size_t write_idx = atomic_load(&stream->write_idx);

    while (ctx->read_idx != write_idx) {
        keying_sample_t sample = stream->samples[ctx->read_idx % STREAM_SIZE];
        ctx->read_idx++;

        // 2. Encode come MORSE frame
        int delta_ms = (sample.timestamp_us - ctx->last_key_timestamp_us) / 1000;
        uint8_t morse_byte = cwstream_encode_timestamp(delta_ms);
        if (sample.key_down) {
            morse_byte |= 0x80;
        }

        // 3. Buffer per invio
        tx_buffer_push(ctx, morse_byte);
        ctx->last_key_timestamp_us = sample.timestamp_us;
    }

    // 4. Flush buffer se necessario
    if (tx_buffer_should_flush(ctx)) {
        send_morse_frame(ctx);
    }
}
```

### 7.4 Remote RX Producer (riceve keying da server remoto)

```c
// Gira su Core 1, scrive nello stream come Producer
typedef struct {
    int socket_fd;
    cwnet_state_t state;
    int64_t base_timestamp_us;          // Base per ricostruzione timestamp
    // ... altri campi
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

        // Ricostruisci timestamp assoluto
        ctx->base_timestamp_us += (int64_t)wait_ms * 1000;

        // Scrivi nello stream (Producer)
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

## 8. Lessons Learned

### 8.1 Bug Critici dall'Implementazione C++

| Bug | Causa | Fix |
|-----|-------|-----|
| Latency drift | Offset timer calcolato una volta | Chiamare `timer_sync_to_server()` ad OGNI ping request |
| CONNECT duplicati | Check `tx_head == tx_tail` per sent | Usare flag booleano `connect_sent` |
| Handshake fail | CONNECT inviato troppo presto | Attendere 100ms dopo TCP connect |
| Callsign rejected | Case sensitivity | Convertire in lowercase |
| Buffer overflow | Audio frames grandi | Buffer 8KB minimo |

### 8.2 Timing Critici

| Operazione | Timing | Note |
|------------|--------|------|
| Post-connect delay | 100ms | Server DL4YHF richiede tempo |
| Handshake timeout | 3000ms | Abort se no ACK |
| Ping interval | 2000ms | Per sync + latency |
| Activity timeout | 5000ms | Server disconnette |
| Poll interval | 20ms | select() timeout |

### 8.3 Pattern da Seguire

```c
// 1. Activity watchdog - inviare QUALCOSA ogni <5s
void feed_activity_watchdog(ctx) {
    if (time_since_last_tx() > 4000) {
        send_ping_request();  // Keep-alive
    }
}

// 2. Non-blocking socket con select()
struct timeval tv = { .tv_sec = 0, .tv_usec = 50000 };  // 50ms
int ready = select(fd + 1, &readfds, &writefds, NULL, &tv);

// 3. Streaming parser per frame frammentati
while (rx_buffer_has_complete_frame()) {
    frame_t frame = parse_frame();
    handle_frame(&frame);
}
```

---

## 9. Implementation Checklist

### 9.1 Core Protocol

- [ ] Frame parser (streaming, gestisce frammentazione)
- [ ] Frame builder (con length encoding corretto)
- [ ] Timer sync (`timer_sync_to_server()` ad ogni ping)
- [ ] CW stream encoder/decoder (7-bit timestamp)
- [ ] A-Law codec (per audio)

### 9.2 Client

- [ ] DNS resolution (non bloccante)
- [ ] TCP connect (non bloccante con select)
- [ ] State machine (IDLE→RESOLVING→CONNECTING→HANDSHAKE→CONNECTED)
- [ ] Handshake: wait 100ms, send CONNECT, wait ACK
- [ ] Ping handling: SYNC timer su request, calc latency su response_2
- [ ] MORSE TX: encode keying events, batch in frames
- [ ] AUDIO RX: decode A-Law, push to audio buffer
- [ ] Activity watchdog: send ping if idle >4s
- [ ] Auto-reconnect on error

### 9.3 Server

- [ ] TCP listen (non bloccante)
- [ ] Accept connection (mono-client per ora)
- [ ] State machine (IDLE→LISTENING→HANDSHAKE→CONNECTED)
- [ ] CONNECT handling: validate, send ACK con permissions
- [ ] Ping initiation: send request periodicamente
- [ ] MORSE RX: decode, push to KeyingStream
- [ ] Permission check: CWNET_PERMISSION_TRANSMIT per MORSE
- [ ] Activity timeout: disconnect dopo 5s inattività

### 9.4 Integration

- [ ] Remote TX Consumer (legge KeyingStream, invia MORSE)
- [ ] Remote RX Producer (riceve MORSE, scrive KeyingStream)
- [ ] Nessun callback (polling-based)
- [ ] Nessuna queue tra componenti
- [ ] Core 1 only (Best-Effort, non RT)

---

## 10. Test Plan

### 10.0 Wireshark Dissector

Un dissector Lua è disponibile in `tools/wireshark/cwnet.lua` per debug del traffico.

**Installazione:**
```bash
# Linux
cp tools/wireshark/cwnet.lua ~/.local/lib/wireshark/plugins/

# Restart Wireshark o Analyze → Reload Lua Plugins
```

**Filtri utili:**
```
cwnet                       # Tutto il traffico CWNet
cwnet.ping.type == 0        # PING REQUEST (sync points!)
cwnet.ping.rtt > 100        # Latenza alta
cwnet.morse.key == 1        # Eventi key-down
cwnet.cmd_type == 0x10      # Solo frame MORSE
```

**Expert Info:**
- `[SYNC POINT]` - Indica dove il client deve chiamare `timer_sync_to_server()`
- RTT calcolato automaticamente sui PING RESPONSE_2

Vedi [tools/wireshark/README.md](../../tools/wireshark/README.md) per dettagli.

### 10.1 Unit Tests (Host)

| Test | Descrizione |
|------|-------------|
| `test_frame_parse` | Parse frame completi e frammentati |
| `test_timestamp_encode` | Verifica encoding 7-bit |
| `test_timestamp_decode` | Verifica decoding 7-bit |
| `test_timer_sync` | Verifica sync non drifta |
| `test_alaw_codec` | Encode/decode round-trip |

### 10.2 Integration Tests (con Server DL4YHF)

| Test | Procedura |
|------|-----------|
| Handshake | Connect, verifica ACK con permissions |
| Latency | 10 ping cycles, verifica stabilità |
| Long session | 1 ora connesso, verifica no drift |
| MORSE TX | Invia CQ, verifica su server |
| AUDIO RX | Verifica ricezione e playback |
| Reconnect | Kill connection, verifica auto-reconnect |

### 10.3 Wireshark Comparison

```bash
# Cattura traffico nostro client
wireshark -i eth0 -f "tcp port 7355" -w our_client.pcap

# Cattura traffico client ufficiale (stesso scenario)
wireshark -i eth0 -f "tcp port 7355" -w official_client.pcap

# Confronta byte-per-byte:
# - CONNECT frame structure
# - PING timestamps (t0, t1, t2)
# - MORSE frame encoding
```

---

## Appendice A: Riferimenti File Sorgente

| File | Contenuto |
|------|-----------|
| `tmp/CwNet.c` | Server/client ufficiale (4177 righe) |
| `tmp/CwStreamEnc.c` | Encoding CW stream (230 righe) |
| `tmp/Timers.c` | Timer sync (310 righe) |
| `tmp/remote_cw_client.cpp` | Nostra impl C++ (riferimento bug) |
| `tmp/docs/RemoteCwNetProtocol.md` | Documentazione protocollo |

---

## Appendice B: Quick Reference

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

**Fine Documento**