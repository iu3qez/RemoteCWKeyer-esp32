# Feature Status - 2026-05-07

## CRITICAL - Missing core

| Feature | Status | File |
|---------|-------|------|
| Winkeyer3 USB (CDC2) | Complete stub, nothing implemented | `keyer_usb/src/usb_winkeyer.c` |
| CWNet RX: CW event callback | RX infrastructure complete, `cw_event_cb = NULL` | `keyer_cwnet/src/cwnet_socket.c` |

## HIGH - Reduce usability

| Feature | Status | File |
|---------|-------|------|
| Timeline page (waveform RT) | ~90% - bug: grid lines not visible | `pages/Timeline.svelte` |
| ~~config_load/save_param()~~ | Removed - bulk load/save_all sufficient | - |

## MEDIUM - Polish

| Feature | Status |
|---------|-------|
| PA/PTT integration (PA enabled unconditionally) | `main.c:226` TODO |
| OTA firmware update | UF2 stub (ROM bootloader reboot only), no HTTP OTA |
| WebUI authentication | Absent (all APIs open) |
| Dark mode, mobile responsive, toast notifications | Not started |
| Latency monitoring (`hal_audio_write`) | Not instrumented |

## Completed (were marked as missing)

| Feature | Actual status |
|---------|-------------|
| Keyer page (text, memory M1-M8) | Complete end-to-end (8 APIs, NVS, UI) |
| WiFi status API | Implemented (status, IP, CWNet latency) |
| CWNet TX (local→remote) | MORSE 0x10 from the keying stream via `cwnet_feed` (#10, #55); not tested on hardware |
| wireguard-vpn | Merged into main (commit `2678037`) - component, console, WebUI, config |
| ESP-IDF v6 migration | Completed - cJSON via component manager, esp_wireguard vendored in `components/`, deps strict-check, GPIO iomux rename |

## Unmerged branches

| Branch | Commits ahead of main | Status |
|--------|----------------------|-------|
| `origin/winkeyer-server` | 4 (morse queue, parser, ui_theme, morse refactor) | WIP phases 1-2, phase 3+ missing |

## Console - WORKING (cleanup done)

- Tab completion: **works**, show-all approach (not cycling)
- History (arrow keys): **complete**
- `line_buffer.c`: deleted (commit `a67bc73`)
- Docs in `console.h`: updated (commit `a67bc73`)

## Tests

- Host tests in `test_host/` exist, iambic FSM coverage uncertain
- Console completion tests: good coverage
