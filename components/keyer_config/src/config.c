/* Auto-generated from parameters.yaml - DO NOT EDIT MANUALLY */
/**
 * @file config.c
 * @brief Configuration initialization
 */

#include "config.h"

keyer_config_t g_config;

void config_init_defaults(keyer_config_t *cfg) {
    atomic_init(&cfg->keyer.wpm, 25);
    atomic_init(&cfg->keyer.iambic_mode, 0);  /* ModeA */
    atomic_init(&cfg->keyer.memory_mode, 3);  /* DOT_AND_DAH */
    atomic_init(&cfg->keyer.squeeze_mode, 0);  /* LATCH_OFF */
    atomic_init(&cfg->keyer.weight, 50);
    atomic_init(&cfg->keyer.mem_window_start_pct, 0);
    atomic_init(&cfg->keyer.mem_window_end_pct, 100);
    atomic_init(&cfg->audio.sidetone_freq_hz, 600);
    atomic_init(&cfg->audio.sidetone_volume, 70);
    atomic_init(&cfg->audio.fade_duration_ms, 5);
    atomic_init(&cfg->hardware.gpio_dit, 4);
    atomic_init(&cfg->hardware.gpio_dah, 5);
    atomic_init(&cfg->hardware.gpio_tx, 6);
    atomic_init(&cfg->timing.ptt_tail_ms, 100);
    atomic_init(&cfg->timing.tick_rate_hz, 10000);
    atomic_init(&cfg->system.debug_logging, false);
    atomic_init(&cfg->system.led_brightness, 50);
    atomic_init(&cfg->generation, 0);
}

void config_bump_generation(keyer_config_t *cfg) {
    atomic_fetch_add_explicit(&cfg->generation, 1, memory_order_release);
}
