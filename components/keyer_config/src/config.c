/* Auto-generated from parameters.yaml - DO NOT EDIT MANUALLY */
/**
 * @file config.c
 * @brief Configuration initialization
 */

#include "config.h"

keyer_config_t g_config;

void config_init_defaults(keyer_config_t *cfg) {
    atomic_init(&cfg->wpm, 25);
    atomic_init(&cfg->iambic_mode, 0);  /* ModeA */
    atomic_init(&cfg->memory_window_us, 500);
    atomic_init(&cfg->weight, 50);
    atomic_init(&cfg->sidetone_freq_hz, 600);
    atomic_init(&cfg->sidetone_volume, 70);
    atomic_init(&cfg->fade_duration_ms, 5);
    atomic_init(&cfg->gpio_dit, 4);
    atomic_init(&cfg->gpio_dah, 5);
    atomic_init(&cfg->gpio_tx, 6);
    atomic_init(&cfg->ptt_tail_ms, 100);
    atomic_init(&cfg->tick_rate_hz, 10000);
    atomic_init(&cfg->debug_logging, false);
    atomic_init(&cfg->led_brightness, 50);
    atomic_init(&cfg->generation, 0);
}

void config_bump_generation(keyer_config_t *cfg) {
    atomic_fetch_add_explicit(&cfg->generation, 1, memory_order_release);
}
