/* Auto-generated from parameters.yaml - DO NOT EDIT MANUALLY */
/**
 * @file config.c
 * @brief Configuration initialization
 */

#include "config.h"
#include <string.h>

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
    atomic_init(&cfg->hardware.gpio_dit, 5);
    atomic_init(&cfg->hardware.gpio_dah, 4);
    atomic_init(&cfg->hardware.gpio_tx, 6);
    atomic_init(&cfg->timing.ptt_tail_ms, 100);
    atomic_init(&cfg->timing.tick_rate_hz, 10000);
    atomic_init(&cfg->system.debug_logging, false);
    strncpy(cfg->system.callsign, "N0CALL", 12);
    cfg->system.callsign[12] = '\0';
    atomic_init(&cfg->system.ui_theme, 0);  /* matrix_green */
    atomic_init(&cfg->leds.gpio_data, 38);
    atomic_init(&cfg->leds.count, 7);
    atomic_init(&cfg->leds.brightness, 50);
    atomic_init(&cfg->leds.brightness_dim, 10);
    atomic_init(&cfg->wifi.enabled, false);
    strncpy(cfg->wifi.ssid, "", 32);
    cfg->wifi.ssid[32] = '\0';
    strncpy(cfg->wifi.password, "", 64);
    cfg->wifi.password[64] = '\0';
    atomic_init(&cfg->wifi.timeout_sec, 30);
    atomic_init(&cfg->wifi.use_static_ip, false);
    strncpy(cfg->wifi.ip_address, "0.0.0.0", 16);
    cfg->wifi.ip_address[16] = '\0';
    strncpy(cfg->wifi.netmask, "255.255.255.0", 16);
    cfg->wifi.netmask[16] = '\0';
    strncpy(cfg->wifi.gateway, "0.0.0.0", 16);
    cfg->wifi.gateway[16] = '\0';
    strncpy(cfg->wifi.dns, "0.0.0.0", 16);
    cfg->wifi.dns[16] = '\0';
    atomic_init(&cfg->vpn.enabled, false);
    strncpy(cfg->vpn.server_endpoint, "", 64);
    cfg->vpn.server_endpoint[64] = '\0';
    atomic_init(&cfg->vpn.server_port, 51820);
    strncpy(cfg->vpn.server_public_key, "", 48);
    cfg->vpn.server_public_key[48] = '\0';
    strncpy(cfg->vpn.client_private_key, "", 48);
    cfg->vpn.client_private_key[48] = '\0';
    strncpy(cfg->vpn.client_address, "10.0.0.2/24", 18);
    cfg->vpn.client_address[18] = '\0';
    strncpy(cfg->vpn.allowed_ips, "0.0.0.0/0", 64);
    cfg->vpn.allowed_ips[64] = '\0';
    atomic_init(&cfg->vpn.persistent_keepalive, 25);
    atomic_init(&cfg->generation, 0);
}

void config_bump_generation(keyer_config_t *cfg) {
    atomic_fetch_add_explicit(&cfg->generation, 1, memory_order_release);
}
