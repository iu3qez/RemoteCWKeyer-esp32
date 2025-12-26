/**
 * @file bg_task.c
 * @brief Background task (Core 1)
 *
 * Best-effort processing:
 * - LED status feedback
 * - WiFi connectivity
 * - Remote CW forwarder
 * - Morse decoder
 * - Diagnostics
 *
 * Runs on Core 1 with normal priority.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include <inttypes.h>
#include <string.h>

#include "keyer_core.h"
#include "rt_log.h"
#include "decoder.h"
#include "text_keyer.h"
#include "text_memory.h"
#include "led.h"
#include "wifi.h"
#include "hal_gpio.h"
#include "config.h"

/* External globals */
extern keying_stream_t g_keying_stream;
extern fault_state_t g_fault_state;

/* Paddle state for text keyer abort (from rt_task.c) */
extern atomic_bool g_paddle_active;

/**
 * @brief Map WiFi state to LED state
 */
static led_state_t wifi_to_led_state(wifi_state_t ws) {
    switch (ws) {
        case WIFI_STATE_DISABLED:
            return LED_STATE_IDLE;  /* Skip to idle if WiFi disabled */
        case WIFI_STATE_CONNECTING:
            return LED_STATE_WIFI_CONNECTING;
        case WIFI_STATE_CONNECTED:
            return LED_STATE_CONNECTED;
        case WIFI_STATE_FAILED:
            return LED_STATE_WIFI_FAILED;
        case WIFI_STATE_AP_MODE:
            return LED_STATE_AP_MODE;
        default:
            return LED_STATE_IDLE;
    }
}

/**
 * @brief Initialize LED from config
 */
static void init_led_from_config(void) {
    led_config_t led_cfg = {
        .gpio_data = CONFIG_GET_GPIO_DATA(),
        .led_count = CONFIG_GET_COUNT(),
        .brightness = CONFIG_GET_BRIGHTNESS(),
        .brightness_dim = CONFIG_GET_BRIGHTNESS_DIM()
    };

    if (led_cfg.led_count == 0) {
        /* LEDs disabled */
        return;
    }

    esp_err_t err = led_init(&led_cfg);
    if (err == ESP_OK) {
        led_set_state(LED_STATE_BOOT);
    }
    /* Non-fatal: continue without LEDs if init fails */
}

/**
 * @brief Initialize WiFi from config
 */
static void init_wifi_from_config(void) {
    if (!CONFIG_GET_ENABLED()) {
        /* WiFi disabled */
        return;
    }

    wifi_config_app_t wifi_cfg = {
        .enabled = true,
        .timeout_sec = CONFIG_GET_TIMEOUT_SEC(),
        .use_static_ip = CONFIG_GET_USE_STATIC_IP()
    };

    /* Copy strings */
    strncpy(wifi_cfg.ssid, CONFIG_GET_SSID(), sizeof(wifi_cfg.ssid) - 1);
    wifi_cfg.ssid[sizeof(wifi_cfg.ssid) - 1] = '\0';

    strncpy(wifi_cfg.password, CONFIG_GET_PASSWORD(), sizeof(wifi_cfg.password) - 1);
    wifi_cfg.password[sizeof(wifi_cfg.password) - 1] = '\0';

    strncpy(wifi_cfg.ip_address, CONFIG_GET_IP_ADDRESS(), sizeof(wifi_cfg.ip_address) - 1);
    wifi_cfg.ip_address[sizeof(wifi_cfg.ip_address) - 1] = '\0';

    strncpy(wifi_cfg.netmask, CONFIG_GET_NETMASK(), sizeof(wifi_cfg.netmask) - 1);
    wifi_cfg.netmask[sizeof(wifi_cfg.netmask) - 1] = '\0';

    strncpy(wifi_cfg.gateway, CONFIG_GET_GATEWAY(), sizeof(wifi_cfg.gateway) - 1);
    wifi_cfg.gateway[sizeof(wifi_cfg.gateway) - 1] = '\0';

    strncpy(wifi_cfg.dns, CONFIG_GET_DNS(), sizeof(wifi_cfg.dns) - 1);
    wifi_cfg.dns[sizeof(wifi_cfg.dns) - 1] = '\0';

    esp_err_t err = wifi_app_init(&wifi_cfg);
    if (err == ESP_OK) {
        /* Set LED to connecting state */
        if (led_is_initialized()) {
            led_set_state(LED_STATE_WIFI_CONNECTING);
        }
        wifi_app_start();
    }
}

void bg_task(void *arg) {
    (void)arg;

    /* Initialize LED driver */
    init_led_from_config();

    /* Initialize decoder (creates its own stream consumer) */
    decoder_init();

    /* Initialize text keyer */
    text_keyer_config_t text_cfg = {
        .stream = &g_keying_stream,
        .paddle_abort = &g_paddle_active,
    };
    text_keyer_init(&text_cfg);
    text_memory_init();

    /* Initialize WiFi */
    init_wifi_from_config();

    /* Log startup */
    int64_t now_us = esp_timer_get_time();
    RT_INFO(&g_bg_log_stream, now_us, "BG task started (text keyer ready)");

    uint32_t stats_counter = 0;
    wifi_state_t prev_wifi_state = WIFI_STATE_DISABLED;
    bool wifi_connected_flash_done = false;

    for (;;) {
        now_us = esp_timer_get_time();

        /* Update LED state from WiFi */
        if (led_is_initialized()) {
            wifi_state_t ws = wifi_get_state();

            /* Detect state changes */
            if (ws != prev_wifi_state) {
                led_state_t new_led_state = wifi_to_led_state(ws);
                led_set_state(new_led_state);

                /* Log state change */
                if (ws == WIFI_STATE_CONNECTED) {
                    char ip_buf[16];
                    if (wifi_get_ip(ip_buf, sizeof(ip_buf))) {
                        RT_INFO(&g_bg_log_stream, now_us, "WiFi connected: %s", ip_buf);
                    }
                    wifi_connected_flash_done = false;
                } else if (ws == WIFI_STATE_AP_MODE) {
                    RT_INFO(&g_bg_log_stream, now_us, "WiFi AP mode active");
                } else if (ws == WIFI_STATE_FAILED) {
                    RT_WARN(&g_bg_log_stream, now_us, "WiFi connection failed");
                }

                prev_wifi_state = ws;
            }

            /* After connected flash sequence, transition to idle */
            led_state_t current_led = led_get_state();
            if (current_led == LED_STATE_IDLE && !wifi_connected_flash_done) {
                wifi_connected_flash_done = true;
            }

            /* Read paddle state for keying overlay */
            gpio_state_t paddles = hal_gpio_read_paddles();
            led_tick(now_us, gpio_dit(paddles), gpio_dah(paddles));
        }

        /* Process decoder (reads from keying_stream) */
        decoder_process();

        /* Tick text keyer (produces samples on keying_stream) */
        text_keyer_tick(now_us);

        /* Periodic stats logging */
        stats_counter++;
        if (stats_counter >= 1000) {  /* Every ~10 seconds at 10ms tick */
            /* Log decoder stats if active */
            if (decoder_is_enabled()) {
                decoder_stats_t stats;
                decoder_get_stats(&stats);
                if (stats.samples_dropped > 0) {
                    RT_WARN(&g_bg_log_stream, now_us, "Decoder dropped: %u samples",
                            (unsigned)stats.samples_dropped);
                }
            }

            /* Check fault state */
            if (fault_is_active(&g_fault_state)) {
                RT_ERROR(&g_bg_log_stream, now_us, "FAULT active: %s (count=%" PRIu32 ")",
                         fault_code_str(fault_get_code(&g_fault_state)),
                         fault_get_count(&g_fault_state));
            }

            stats_counter = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(10));  /* 100Hz - adequate for LED animations */
    }
}
