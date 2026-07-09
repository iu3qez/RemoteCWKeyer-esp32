<!-- BEGIN treecode (auto) — do not edit inside this block -->
# keyer_console — Interactive serial command interface (Core 1)

Responsibility: Owns the line-editing state machine and command registry for the
interactive text console. Parses input a character at a time, maintains history and
tab completion, dispatches parsed commands to handlers, and prints results. It is a
user-facing interface: it runs only on Core 1 (background) and must NEVER touch the
hard RT path or block it. It does not own the USB transport — bytes are fed in by
keyer_usb; on-host builds it can echo directly.

Key abstractions:
- `console_push_char()` / `console_process_char()` — feed one input byte; return true
  when a full command line was executed (caller should reprint the prompt).
- `console_parse_line()` → `console_parsed_cmd_t` (command + up to 3 args).
- Command registry: `console_cmd_t`, `console_get_commands()`, `console_find_command()`,
  `console_execute()`; handlers return `console_error_t` (E01..E06 codes).
- History ring (`console_history_*`) and tab completion (`console_complete`).
- `console_task()` — Core 1 loop (getchar) for non-USB/host use.

Depends on: keyer_config, keyer_logging, keyer_hal, keyer_decoder, keyer_text,
esp_driver_usb_serial_jtag, esp_timer (REQUIRES); keyer_usb, keyer_wifi, keyer_vpn
(PRIV_REQUIRES, for command handlers that touch USB/WiFi/VPN state).

Used by: main/bg_task.c and keyer_usb (usb_console.c pushes RX bytes into the state
machine). Prompt shows the configured callsign from g_config.

External deps of note: On ESP builds `printf` is redefined to `usb_console_printf`
(see top of console.c / commands.c) so output goes to CDC0, not stdout.

Conventions: Fixed-size static buffers only (CONSOLE_LINE_MAX=64, no heap). Errors are
returned as `console_error_t`, never printed by handlers directly. CMakeLists runs
gen_log_tags.py at configure time to produce log_tags.h. Built with -Wconversion
-Wshadow.

Gotchas: Circular-looking dep with keyer_usb is intentional and one-way at link time
(console PRIV_REQUIRES keyer_usb; usb only includes console.h). Arrow keys / Ctrl-C /
Ctrl-U are handled via an ANSI escape state machine inside push_char — feeding raw
bytes out of order will desync it. `console_complete_reset()` is a retained no-op.
<!-- END treecode (auto) -->
