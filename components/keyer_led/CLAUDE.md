<!-- BEGIN treecode (auto) — do not edit inside this block -->
# keyer_led — WS2812B status LED strip: five situations, one colour, a paddle overlay (Core 1)

Responsibility: Owns what the strip shows. `led_render.h`/`led_render.c` are the pure half — no ESP-IDF, host-tested by `test_host/test_led.c` — and own the colour vocabulary, the five situations, the animation clocks, the paddle overlay and `led_render()` itself. `src/led.c` is only the RMT shell: it owns the WS2812B bit encoding, the strip's atomic situation/notify state, and calls `led_render()` on tick. Purely a display sink — it must NOT influence keying, TX, or the stream, and the DIT/DAH it reads are for display, never a keying source.

Key abstractions:
- `led_situation_t` (`led_render.h`): five values, one colour each — `STARTING` (orange, breathing), `SETUP` (blue, breathing), `LOCAL_ONLY` (yellow, dim), `ON_AIR` (green, dim), `OFF_AIR` (red, dim). The governing rule, stated at the top of `led_render.h`: green means the operator's CW goes out; any other colour means it does not, and the colour says why. Nothing but `LED_SITUATION_ON_AIR` is ever green — magenta and the old nine-value `led_state_t` are gone.
- `led_render(const led_scene_t*, led_frame_t*)`: pure function, scene in, per-LED colour frame out. Base colour/level from the situation (breathing for STARTING/SETUP, `brightness_dim` otherwise); a running notification overrides the level with flashes in that same colour; the paddle overlay then lights its LEDs at full brightness in that same colour. No step introduces a colour of its own.
- `led_notify()`: an event, not a state — three attention flashes (`LED_NOTIFY_FLASHES`, 100ms on/100ms gap) in the colour of whatever situation they land on, so a notification can never assert a colour the situation contradicts.
- The paddle overlay: raw, undebounced DIT/DAH pins, applied in every situation (previously idle-only). It carries position and brightness only, never its own colour — lit-but-silent means a software fault, dark means the contact or wiring, since it is fed upstream of debounce and the FSM.
- `led_set_situation()`/`led_get_situation()`: idempotent — setting the situation already showing does not restart its clock, so `bg_task` can call it every tick from a mapping. `led_tick(now_us, dit, dah)`: builds the `led_scene_t`, calls `led_render()`, pushes the frame over RMT.
- `led_config_t` (gpio, count, brightness, brightness_dim) with `LED_CONFIG_DEFAULT` (GPIO38, 7 LEDs); `led_set_brightness()`. `LED_MAX_COUNT` (16, public header) replaces the old `MAX_LEDS`.

Depends on: driver, esp_driver_rmt (WS2812B bit-banging via RMT), esp_timer (situation/notify clocks). `led_render.c` itself depends on nothing beyond the standard headers in `led_render.h`.

Used by: main/bg_task.c's `led_situation_now(wifi_state_t)` maps WiFi state and, once connected, `cwnet_socket_can_transmit()` to a situation, called every tick (the key can change hands without WiFi moving); it calls `led_notify()` on WiFi connect/fail transitions and `led_tick()` with the raw paddle read from `hal_gpio_read_paddles()`. main.c sets `LED_SITUATION_STARTING` after init and on WiFi bring-up, `LED_SITUATION_LOCAL_ONLY` on WiFi init failure, `LED_SITUATION_ON_AIR` when WiFi is disabled by config. components/provisioning/src/provisioning.c sets `LED_SITUATION_SETUP` and drives `led_tick()` itself during the AP-mode provisioning loop.

External deps of note: esp_driver_rmt — the WS2812B timing constants (T0H/T0L/T1H/T1L ticks at 10MHz/100ns resolution, plus a low reset symbol) live in `led.c`'s custom bytes+copy strip encoder. Changing them breaks the LED protocol, not just brightness.

Conventions: Built with -Wall -Wextra -Werror -Wconversion -Wshadow (strictest in the tree). Colours are 0xRRGGBB, one constant per situation and no spares (`LED_COLOR_OFF/RED/GREEN/BLUE/ORANGE/YELLOW` — magenta was removed) so a colour that belongs to nothing cannot be misread. `led_render.h`/`.c` stay free of ESP-IDF so they build under `test_host`; hardware concerns belong in `led.c`.

Gotchas:
- Runs on Core 1 only — never call from the RT task.
- Init failure is deliberately non-fatal: always guard with `led_is_initialized()` and let the keyer continue if the strip is absent; `led_tick`/`transmit_leds` also no-op when uninitialized.
- WS2812B is GRB on the wire; the encoder handles ordering — pass logical 0xRRGGBB into `led_render`/`led.c`, don't pre-swap.
- Only `led.c` (the RMT shell) may know about ESP-IDF or hardware; any new display logic — a colour, a clock, an overlay rule — belongs in `led_render.c` so it stays provable by `test_host/test_led.c` without a strip attached.
- The paddle overlay is fed raw pins from `bg_task`'s own `hal_gpio_read_paddles()` call, not the debounced/FSM state used for keying — it is a deliberate second, independent read for hardware-vs-software fault diagnosis; don't collapse it into a single read path.
<!-- END treecode (auto) -->
