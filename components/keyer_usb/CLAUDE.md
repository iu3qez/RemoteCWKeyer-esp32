<!-- BEGIN treecode (auto) — do not edit inside this block -->
# keyer_usb — TinyUSB multi-CDC transport (Core 1)

Responsibility: Owns the USB device stack (TinyUSB) and exposes three composite CDC-ACM
interfaces plus UF2 firmware update. It is the physical transport for the user-facing
console and logs; it runs entirely on Core 1 and must never block the RT path. It moves
bytes only — command parsing lives in keyer_console, log formatting in keyer_logging.

Key abstractions:
- `usb_cdc_init()` / `usb_cdc_write()` / `usb_cdc_flush()` / `usb_cdc_connected()` — raw
  per-interface I/O. CDC0=console, CDC1=log, CDC2=Winkeyer (reserved).
- `usb_console_*` — CDC0 RX callback echoes and pushes chars into console_push_char;
  `usb_console_printf()` is the console output sink.
- `usb_log_*` — CDC1 drain task pulls the RT-safe log ring buffer and applies
  global/per-tag level filters (`usb_log_set_level`, `usb_log_set_tag_level`).
- `usb_uf2_init()` / `usb_uf2_enter()` — reboot into UF2 mass-storage bootloader.
- `usb_winkeyer_*` — stub for future K1EL Winkeyer3 emulation (not implemented).

Depends on: esp_tinyusb (registry, ^1.0.0), keyer_logging, esp_system (REQUIRES);
freertos, esp_timer, keyer_console (PRIV_REQUIRES).

Used by: main.c (usb_cdc_init early in app_main) and bg_task.c (usb_log_task). Console
commands in keyer_console call into usb_uf2 / usb_log / usb_console.

External deps of note: espressif/esp_tinyusb declared in idf_component.yml; USB VID/PID
0x303A/0x8002, serial "KEYER-IU3QEZ-001". UF2 path uses esp_tinyuf2.

Conventions: init order matters — usb_cdc_init() must run before console_task starts.
Built with -Wno-unused-parameter (TinyUSB callback signatures).

Gotchas: CDC2/Winkeyer is a stub — `usb_winkeyer_is_enabled()` always returns false.
The log drain is a Core 1 task, not called from RT code; RT producers only enqueue into
the ring buffer. Entering UF2 (`usb_uf2_enter`) restarts the device and does not return.
<!-- END treecode (auto) -->
