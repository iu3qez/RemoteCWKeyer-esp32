<!-- BEGIN treecode (auto) — do not edit inside this block -->
# keyer_cwnet — remote CW-over-network transport (CWNet protocol)

Responsibility: Implements the CWNet TCP protocol for streaming key-down/key-up
events to/from a remote server, plus server time synchronization. Owns the wire
format, the client state machine, and the ESP-IDF socket glue. It must NOT touch
the hard RT path, allocate on the RT path, or read/write the keying stream
directly — bg_task (Core 1) pulls samples from a best-effort consumer and hands
key events to this module.

Key abstractions:
- cwnet_client_t / cwnet_client_config_t: transport-agnostic state machine
  (DISCONNECTED -> CONNECTING -> READY). Socket I/O is injected via send_cb /
  get_time_ms_cb callbacks so it is testable without a network.
- cwnet_frame_parser_t: streaming, fragmentation-tolerant frame parser
  (command byte encodes category in bits 7-6, command in bits 5-0).
- cwnet_ping_t / cwnet_timer_t: PING-based clock sync and RTT/latency.
- cwstream_encode/decode_timestamp: 7-bit non-linear ms timestamp codec.
- cwnet_socket_*: the concrete ESP-IDF layer (BSD sockets + lwIP DNS) that owns
  the real TCP socket, reconnection, and NVS config; driven by
  cwnet_socket_process() at ~100Hz.

Depends on: keyer_config, keyer_logging, esp_timer, lwip.
Used by: main/bg_task.c (init + process + send_key_event), keyer_webui
  (api_system.c, for state/latency reporting).
External deps of note: lwIP BSD sockets + getaddrinfo (blocking DNS, non-blocking
  connect via select()); esp_timer for local time; RT_*() logging into
  g_bg_log_stream, not ESP_LOGx.

Conventions: The pure protocol layer (client/frame/ping/timestamp) never calls
socket APIs — only cwnet_socket.c does. Config comes from g_config.remote.
-Wconversion/-Wshadow/-Wstrict-prototypes are enforced.

Gotchas:
- Timer MUST resync on every PING REQUEST or timestamps drift and the server
  rejects packets.
- cwnet_socket runs entirely on Core 1; it is best-effort and never blocks the
  keyer. cwnet_socket_init() is a no-op unless remote.cwnet_enabled in NVS.
- The client does not own its socket; the socket layer must forward
  on_connected / on_data / on_disconnected events into it.
- Frame parser copies only small fragmented payloads into its 256-byte buffer;
  large payloads are returned as pointers into the input buffer (do not retain).
<!-- END treecode (auto) -->
