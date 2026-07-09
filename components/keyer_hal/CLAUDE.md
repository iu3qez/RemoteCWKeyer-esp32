<!-- BEGIN treecode (auto) — do not edit inside this block -->
# keyer_hal — Hardware abstraction: paddle/TX GPIO, I2S audio, ES8311 codec, TCA9555 PA

Responsibility: Owns all direct hardware access so the rest of the keyer stays hardware-free. Reads paddle GPIO, drives the TX line, and provides audio output (I2S DMA + ES8311 codec over I2C, plus PA enable via TCA9555 IO expander). It must NOT contain keying logic — it only reads inputs and pushes bytes/levels out.

Key abstractions:
- GPIO (`hal_gpio.h`): `hal_gpio_init`, `hal_gpio_read_paddles` → `gpio_state_t`, `hal_gpio_set_tx/get_tx`. ISR-based press detection with `hal_gpio_consume_dit_press`/`consume_dah_press` (atomic-exchange, RT-safe) and `hal_gpio_isr_tick(now_us)` which the RT task must call every tick.
- Audio (`hal_audio.h`): `hal_audio_init` (ES8311 + I2S + TCA9555), `hal_audio_write(int16 mono samples)`, `hal_audio_start/stop`, `hal_audio_set_volume/set_pa` (I2C, NOT RT-safe), `hal_audio_is_available`.
- Config structs `hal_gpio_config_t` / `hal_audio_config_t` with *_DEFAULT macros matching the reference board.

Depends on: keyer_core, driver, esp_driver_gpio, esp_driver_i2s, esp_driver_i2c, esp_timer. PRIV_REQUIRES esp_codec_dev, esp_io_expander, esp_io_expander_tca95xx_16bit (declared in idf_component.yml).
Used by: main/rt_task (paddle read + TX + audio write on Core 0) and audio setup during boot.
External deps of note: esp_codec_dev drives the ES8311; the TCA9555 16-bit expander gates the power amp; uses esp_private/gpio.h `gpio_iomux_output/input` (v6 rename) in force_gpio_reset.

Conventions:
- Built with -Wconversion -Wshadow.
- ISR handlers are IRAM_ATTR and use ONLY ISR-safe ops (atomic_store, disable interrupt) — see the header comment block in hal_gpio.c.
- Every driver include must have a matching REQUIRES entry (ESP-IDF v6 strict transitive-deps).

Gotchas:
- `esp_timer_start_once()` is NOT ISR-safe. Pattern: ISR sets an atomic "needs blanking" flag; `hal_gpio_isr_tick` (task context) actually starts the blanking timer and runs a stuck-interrupt watchdog. This adds ~1ms to blanking start — do not move timer starts into the ISR.
- `isr_blanking_us = 0` disables the ISR and falls back to pure polling.
- Audio init failing must NOT block boot — the system degrades gracefully (check `hal_audio_is_available`).
- set_volume/set_pa do I2C transactions — never call them from the Core 0 RT path.
<!-- END treecode (auto) -->
