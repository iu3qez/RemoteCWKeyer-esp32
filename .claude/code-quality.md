# Code Quality Notes - 2026-09-01

## Recent Fixes (2026-09-01, branch claude/remote-environment-setup-vdjx7v)
- Host tests: 8 pre-existing failures fixed, suite 189/189 green with ASan/UBSan too.
  - `iambic.c`: release debounce triggered itself at t=0 (`now - release_time(0) < 5ms`); release timestamps now start at `-IAMBIC_DEBOUNCE_RELEASE_US`.
  - `iambic.c`: `progress_pct` computed in int64, no longer truncated to uint8 (a release beyond 255% wrapped below the window).
  - `cwnet_frame.c`: explicit cast on `payload_len` (`-Wconversion` under UBSan).
  - `test_stream.c`: the lag test uses `stream_push_raw()` (it was written before silence compression).
  - `test_fault.c`: `fault_clear()` does not reset `count` - it is a lifetime counter (`fault.h`), the test expected the reset.
- CI: `.github/workflows/host-tests.yml` (plain + asan-ubsan). Before, no build/test CI existed.

## To fix
- `consumer_resync()` (`components/keyer_core/src/stream.c`) lands `capacity - 1` behind, that is on the last readable slot: on the next push the consumer is in overrun again unless it reads first. No production caller today; from #70.
- `main/rt_task.c`, config re-read: the `continue` on a torn read also skips `vTaskDelayUntil()` at the bottom of the loop, so the retry runs immediately instead of waiting for the tick. Harmless (one extra iteration), but the tick is no longer periodic in that case; from #71.
- `.devcontainer/Dockerfile`: `ARG DOCKER_TAG=v5.5.1` and path `idf5.5_py3.12_env` hardcoded - not updated after the migration to IDF v6. To be verified on an `espressif/idf:v6.x` image before changing.
- `test_host/CLAUDE.md` (auto `treecode` block) still names `keyer_iambic` as an in-tree dependency: since 2026-09-06 it is the `Esp32KeyerTest` submodule. `components/keyer_cwnet/CLAUDE.md` still says that it is `bg_task` that passes the keying events: since 2026-09-07 the module consumes the stream itself (`cwnet_feed`, #55); and that the config comes only from `g_config.remote`: since 2026-09-08 the CONNECT carries `g_config.system.callsign` (#25). They are resynced with `map-tree`, not by hand (`keyer_core` regenerated with the cartographer on 2026-09-08, #57).
- `sample_silence()` clamps the marker to `UINT16_MAX`: beyond 65.5 s of unchanged stream the excess ticks vanish from the marker and the stream time rebuilt by a consumer (`cwnet_feed`) falls behind. Harmless as long as the end of over closes the over; the remedy is for `stream_push` to flush the marker when `idle_ticks` reaches `UINT16_MAX` (RT path, one comparison per tick, host test possible).
- Remote branch `k8-differential-bench` (`fae2f57`), with no PR: the bench skeleton moved to Esp32KeyerTest, `FINDINGS.md` (superseded numbers) lives only there. To be deleted when nobody cites it any more.


## Context: CWNet PING, what is in the three timestamps

The DL4YHF client answers the REQUEST by copying the three timestamps of its array (`CwNet.c:1478`): slot 0 = the server's `t0`, slot 1 = its raw local clock (`:1403`), slot 2 = the `t2` of the previous round, never cleared. Ours puts the synchronized time in slot 1 and 0 in slot 2. The server copies slot 1 into RESPONSE_2 and overwrites slot 2; nobody reads slot 1 on the other side. Different bytes, no effect. Observed while building `cwnet_echo.py` (#14).

## Cleanup Items

### Vendored esp_wireguard - GCC 15 / ESP-IDF v6 patches
- `components/esp_wireguard/` is an in-tree fork of `trombik/esp_wireguard 0.9.0` (the only version on the registry, not updated for v6).
- Patches applied:
  - `wireguard-platform.c`: removed the `mbedtls_entropy/ctr_drbg` path, now uses `esp_fill_random()` (the standalone APIs are no longer linked in mbedtls 4).
  - `CMakeLists.txt`: `-Wno-error=stringop-overread` (already upstream, for IDF v5 only) extended to v6, plus `-Wno-error=unterminated-string-initialization` for `wireguard.c` (protocol constants as byte arrays of 8/34/37).
- To do: watch whether upstream publishes a v6-compatible version, so it can be un-vendored.

### Resolved (commit a67bc73)
- ~~`line_buffer.c` orphan stub~~ - deleted
- ~~Stale docs in `console.h`~~ - updated

### Parser scaffolding (not dead code)
- `parser.c`: `skip_whitespace()` and `find_token_end()` marked `__attribute__((unused))` - reserved for future use, intentional

### Active stubs (to implement or remove)
- `keyer_usb/src/usb_winkeyer.c` - complete stub, `usb_winkeyer_is_enabled()` always returns false
- `keyer_usb/src/usb_uf2.c` - only `esp_restart()`, no real UF2 (esp_tinyuf2/esp_tinyusb conflict)
- `config_nvs.c:551,557` - `config_load_param()`/`config_save_param()` return `ESP_ERR_NOT_SUPPORTED`

## Recent Fixes (2026-02-15, on main)
- WebSocket: static 256B buffer (removed unbounded malloc)
- NVS: rate limiting 10s per save
- Config hot-reload: optimistic generation re-check in rt_task.c
- fade_duration_ms: clamp before the uint16_t cast
- log_stream_push: relaxed memory ordering for read_idx

## Station daemon (2026-09-10, #64)

- **The DL4YHF archive is missing only the author's personal libraries.**
  The protocol files are all there: `HamlibResultCodes.h`, given as missing
  by the daemon plan, is in the archive (2051 bytes). Before writing "X is
  not there", `unzip -l ~/Downloads/Remote_CW_Keyer_Sources.zip | grep -i <nome>`.
- **A split wait and an end of over are identical on the wire**: consecutive
  bytes with the same key state. `cwnet_play` tells them apart with the
  encoder's convention (`CwStreamEnc.c:135-146`): every piece except the last
  carries the 7-bit field at its maximum. The cost, written in `cwnet_play.h`, is that
  a wait exactly as long as the maximum encodable value loses its edge and
  sounds like silence. The reference's receiver instead applies it at once and
  keys up to 830 ms early: that is corrupted timing, and it is not copied.
- **On macOS, Python's `time.monotonic()` and the daemon's `CLOCK_MONOTONIC` do not
  share the epoch**, on Linux they do. `tools/cwnet/cwnet_jitter.py` calibrates
  on the first edge for this reason; on Linux that calibration hides a
  check that would be genuine there. See #76.
