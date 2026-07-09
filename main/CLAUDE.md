<!-- BEGIN treecode (auto) — do not edit inside this block -->
# main — firmware entry point and the two per-core RT/BG tasks

Responsibility: Owns `app_main()` boot sequence and the two pinned FreeRTOS tasks that
drive the whole keyer. It wires components together (NVS, config, HAL, WiFi/VPN, WebUI,
decoder, console, streams) and defines the process-global singletons
(`g_keying_stream`, `g_fault_state`, `g_paddle_active`). It must NOT contain keying,
audio, or protocol logic — that lives in the `keyer_*` components; main only
orchestrates init and hosts the task loops.

Key abstractions:
- `app_main()` (main.c): ordered boot — logs → NVS → factory-reset/provisioning check
  → config load → netif → HAL GPIO → USB CDC → LED → WiFi → VPN → stream/fault init →
  audio → console → WebUI → decoder → text keyer, then spawns tasks and waits for USB CDC.
- `rt_task()` (rt_task.c): the 1ms hard-RT loop — GPIO poll → iambic tick → stream_push →
  hard_rt_consumer → TX GPIO + I2S sidetone → PTT. Also does IDLE-only config hot-reload.
- `bg_task()` (bg_task.c): 10ms best-effort loop — LED/WiFi/VPN state, CWNet socket,
  decoder, WebUI timeline push, text keyer tick, periodic stats.
- `audio_test.c`: standalone 600Hz tone task for bringing up the ES8311 codec (not in boot path).

Depends on: essentially every component — keyer_core, keyer_iambic, keyer_audio,
keyer_logging, keyer_console, keyer_usb, keyer_config, keyer_hal, keyer_decoder,
keyer_text, keyer_led, keyer_wifi, keyer_vpn, keyer_webui, keyer_cwnet, provisioning,
plus freertos and esp_timer.

Used by: nobody — this is the top of the tree; it becomes the firmware image.

External deps of note: ESP-IDF FreeRTOS (`xTaskCreatePinnedToCore`), NVS, esp_netif/esp_event,
esp_timer for the monotonic `now_us` clock threaded through both loops.

Conventions: Do ALL component init in `app_main` (bg_task explicitly assumes this — it
re-initializes nothing but the timeline consumer and CWNet). Config is read via the
generated `CONFIG_GET_*()` macros. Globals cross cores only through atomics or the stream.

Gotchas:
- rt_task is pinned to Core 0 at `configMAX_PRIORITIES-1` and is the hard RT path:
  NO malloc/log/mutex/printf on it — use `RT_*` macros and `stdatomic.h` only (ARCHITECTURE.md).
- Iambic/sidetone/PTT config hot-reload happens ONLY when the FSM is IDLE, guarded by a
  config `generation` counter with a torn-read retry; never reload mid-element.
- bg_task, usb_log, uart_log are all pinned to Core 1; uart_log is deleted once USB CDC connects.
- On stream_push failure rt_task raises `FAULT_PRODUCER_OVERRUN` — corrupted timing must FAULT, not limp.
<!-- END treecode (auto) -->
