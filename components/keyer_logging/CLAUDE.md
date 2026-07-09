<!-- BEGIN treecode (auto) — do not edit inside this block -->
# keyer_logging — RT-safe non-blocking logging (lock-free push, Core-1 UART drain)

Responsibility: Owns logging that is safe to call from the hard-RT path. The producer side (RT_*() macros) pushes formatted entries into a lock-free ring in ~100-200ns and never blocks; a Core-1 task drains them to UART. It exists precisely so the RT path never calls ESP_LOGx/printf. It must NOT block, allocate, or do I/O on the producer side — draining and UART writes happen off Core 0.

Key abstractions:
- `log_stream_t`: fixed 256-entry (`LOG_BUFFER_SIZE`) ring of `log_entry_t` (timestamp, level, ≤120-char msg), with atomic write/read/dropped indices. Globals `g_rt_log_stream` (Core 0) and `g_bg_log_stream` (Core 1).
- Producer: `log_stream_push` / the `RT_ERROR/WARN/INFO/DEBUG/TRACE(stream, ts, fmt, ...)` macros. Diagnostic variants `RT_DIAG_*` gate on the atomic `g_rt_diag_enabled` flag (single relaxed load when off).
- Consumer: `log_stream_drain`, `log_stream_count/has_entries/dropped`, and `uart_logger_init` + `uart_logger_task` (drains both streams to UART1 @115200 on Core 1).

Depends on: keyer_core, driver, esp_driver_uart, esp_driver_gpio, esp_timer.
Used by: every component/task that logs from or near the RT path — main/rt_task, bg_task, and anything on Core 0. The uart_log task (Core 1) is the sole drainer.
External deps of note: esp_driver_uart (UART1 output), esp_timer (timestamps). snprintf is used inside RT_LOG on a stack buffer.

Conventions:
- Built with -Wconversion -Wshadow.
- On the RT/producer side use ONLY the RT_*/RT_DIAG_* macros — never ESP_LOGx or printf.
- Caller passes the timestamp (`ts`, µs) into the macros; keep it consistent with esp_timer_get_time().
- Two separate streams keep Core-0 and Core-1 producers from contending.

Gotchas:
- The ring is bounded: on overflow entries are DROPPED (dropped counter increments), never blocked — silence beats stalling the RT path. Check the dropped count if logs go missing.
- Messages over LOG_MAX_MSG_LEN (120) are truncated.
- RT_LOG still runs snprintf on the producer — cheap but not free; keep hot-loop TRACE behind RT_DIAG_* so it compiles to a single atomic check when diagnostics are off.
- The drain task must actually be scheduled on Core 1, or the ring fills and everything after is dropped.
<!-- END treecode (auto) -->
