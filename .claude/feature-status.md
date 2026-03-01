# Feature Status — 2026-03-01

## CRITICAL — Core mancanti

| Feature | Stato | File |
|---------|-------|------|
| Winkeyer3 USB (CDC2) | Stub completo, nulla implementato | `keyer_usb/src/usb_winkeyer.c` |
| CWNet RX: callback eventi CW | Infrastruttura RX completa, `cw_event_cb = NULL` | `keyer_cwnet/src/cwnet_socket.c` |

## HIGH — Riducono usabilità

| Feature | Stato | File |
|---------|-------|------|
| Timeline page (waveform RT) | ~90% — bug: grid lines non visibili | `pages/Timeline.svelte` |
| ~~config_load/save_param()~~ | Rimossi — bulk load/save_all sufficiente | — |

## MEDIUM — Polish

| Feature | Stato |
|---------|-------|
| PA/PTT integration (PA abilitato incondizionatamente) | `main.c:226` TODO |
| OTA firmware update | UF2 stub (solo reboot ROM bootloader), no HTTP OTA |
| WebUI authentication | Assente (API tutte aperte) |
| Dark mode, mobile responsive, toast notifications | Non iniziati |
| Latency monitoring (`hal_audio_write`) | Non strumentato |

## Completati (erano segnati come mancanti)

| Feature | Stato reale |
|---------|-------------|
| Keyer page (testo, memory M1-M8) | Completa end-to-end (8 API, NVS, UI) |
| WiFi status API | Implementata (stato, IP, CWNet latency) |
| CWNet TX (locale→remoto) | Funzionante via `cwnet_socket_send_key_event()` |
| wireguard-vpn core | Mergiato su main (commit `1412461`) |

## Branch non mergiati

| Branch | Commit avanti di main | Stato |
|--------|----------------------|-------|
| `origin/wireguard-vpn` | 4 (WebUI panel, sdkconfig, config family, handoff) | Parziale — core mergiato, UI/config no |
| `origin/winkeyer-server` | 4 (morse queue, parser, ui_theme, morse refactor) | WIP fasi 1-2, fase 3+ mancante |

## Console — FUNZIONANTE (cleanup fatto)

- Tab completion: **funziona**, approccio show-all (non cycling)
- History (arrow keys): **completa**
- `line_buffer.c`: cancellato (commit `a67bc73`)
- Docs in `console.h`: aggiornati (commit `a67bc73`)

## Test

- Host test in `test_host/` esistono, copertura FSM iambic incerta
- Test console completion: copertura buona
