# Code Quality Notes — 2026-03-01

## Cleanup Items

### Risolti (commit a67bc73)
- ~~`line_buffer.c` stub orfano~~ — cancellato
- ~~Docs stale in `console.h`~~ — aggiornati

### Parser scaffolding (non dead code)
- `parser.c`: `skip_whitespace()` e `find_token_end()` marcati `__attribute__((unused))` — riservati per uso futuro, intenzionali

### Stubs attivi (da implementare o rimuovere)
- `keyer_usb/src/usb_winkeyer.c` — stub completo, `usb_winkeyer_is_enabled()` ritorna sempre false
- `keyer_usb/src/usb_uf2.c` — solo `esp_restart()`, no UF2 reale (conflitto esp_tinyuf2/esp_tinyusb)
- `config_nvs.c:551,557` — `config_load_param()`/`config_save_param()` ritornano `ESP_ERR_NOT_SUPPORTED`

## Recent Fixes (2026-02-15, su main)
- WebSocket: buffer statico 256B (rimosso malloc illimitato)
- NVS: rate limiting 10s per save
- Config hot-reload: optimistic generation re-check in rt_task.c
- fade_duration_ms: clamp prima del cast uint16_t
- log_stream_push: memory ordering rilassato per read_idx
