<!-- BEGIN treecode (auto) — do not edit inside this block -->
# keyer_led — WS2812B RGB status LED driver (Core 1)

Responsibility: Owns the WS2812B status LED strip: a state machine of connectivity/keyer states (boot, wifi-connecting, failed, degraded, AP mode, provisioning, connected, idle) rendered as breathing/flashing animations, plus a real-time keying overlay (DIT/DAH/squeeze) driven by the paddle inputs. Purely a display sink — it must NOT influence keying, TX, or the stream.

Key abstractions:
- `led_init(led_config_t)` / `led_deinit()` — sets up the RMT TX channel and a custom byte+copy strip encoder; init is non-fatal (keyer runs on if it fails).
- `led_set_state(led_state_t)` / `led_get_state()` — pick the base animation.
- `led_tick(now_us, dit, dah)` — call periodically from bg_task; advances animations and overlays live paddle state.
- `led_config_t` (gpio, count, brightness, brightness_dim) with `LED_CONFIG_DEFAULT` (GPIO38, 7 LEDs); `led_set_brightness()`.

Depends on: driver, esp_driver_rmt (WS2812B bit-banging via RMT), esp_timer (animation clock).

Used by: main/bg_task.c calls `led_tick` with current paddle state; main sets states on lifecycle events; provisioning drives AP/provisioning states.

External deps of note: esp_driver_rmt — timing constants encode the WS2812B protocol (T0H/T0L/T1H/T1L ticks at 10MHz/100ns resolution, plus a reset symbol). Changing them breaks the LED protocol, not just brightness.

Conventions: Built with -Wall -Wextra -Werror -Wconversion -Wshadow (strictest in the tree). Colors are 0xRRGGBB; MAX_LEDS 16.

Gotchas:
- Runs on Core 1 only — never call from the RT task. It is a status sink; the DIT/DAH args are for display, not a keying source.
- Init failure is deliberately non-fatal: always guard with `led_is_initialized()` and let the keyer continue if the strip is absent.
- WS2812B is GRB on the wire; the encoder handles ordering — pass logical 0xRRGGBB, don't pre-swap.
<!-- END treecode (auto) -->
