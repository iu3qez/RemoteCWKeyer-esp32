# Code Quality Notes — 2026-09-01

## Recent Fixes (2026-09-01, branch claude/remote-environment-setup-vdjx7v)
- Host tests: 8 failures pre-esistenti risolti, suite 189/189 verde anche con ASan/UBSan.
  - `iambic.c`: release debounce si autoattivava a t=0 (`now - release_time(0) < 5ms`); i timestamp di rilascio partono ora a `-IAMBIC_DEBOUNCE_RELEASE_US`.
  - `iambic.c`: `progress_pct` calcolato in int64, non più troncato a uint8 (un rilascio oltre 255% wrappava sotto la finestra).
  - `cwnet_frame.c`: cast esplicito su `payload_len` (`-Wconversion` sotto UBSan).
  - `test_stream.c`: il test di lag usa `stream_push_raw()` (era scritto prima della silence compression).
  - `test_fault.c`: `fault_clear()` non azzera `count` — è un contatore lifetime (`fault.h`), il test si aspettava il reset.
- CI: `.github/workflows/host-tests.yml` (plain + asan-ubsan). Prima non esisteva alcuna CI di build/test.

## Da sistemare
- `.devcontainer/Dockerfile`: `ARG DOCKER_TAG=v5.5.1` e path `idf5.5_py3.12_env` hardcoded — non aggiornati dopo la migrazione a IDF v6. Da verificare su un'immagine `espressif/idf:v6.x` prima di cambiare.


## Cleanup Items

### Vendored esp_wireguard — GCC 15 / ESP-IDF v6 patches
- `components/esp_wireguard/` è un fork in-tree di `trombik/esp_wireguard 0.9.0` (l'unica versione sul registry, non aggiornata per v6).
- Patch applicate:
  - `wireguard-platform.c`: rimosso il giro `mbedtls_entropy/ctr_drbg`, ora usa `esp_fill_random()` (le API standalone non sono più linkate in mbedtls 4).
  - `CMakeLists.txt`: `-Wno-error=stringop-overread` (già nell'upstream solo per IDF v5) esteso a v6, più `-Wno-error=unterminated-string-initialization` per `wireguard.c` (costanti del protocollo come byte array da 8/34/37).
- Da fare: monitorare se upstream pubblica una versione v6-compatible per dewirevendorare.

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
