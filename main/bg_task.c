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
#include "webui.h"

/* External globals */
extern keying_stream_t g_keying_stream;
extern fault_state_t g_fault_state;

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

void bg_task(void *arg) {
    (void)arg;

    /* Note: All initialization (LED, WiFi, decoder, text_keyer) is done in main.c */

    /* Log startup */
    int64_t now_us = esp_timer_get_time();
    RT_INFO(&g_bg_log_stream, now_us, "BG task started (text keyer ready)");

    uint32_t stats_counter = 0;
    wifi_state_t prev_wifi_state = WIFI_STATE_DISABLED;
    bool wifi_connected_flash_done = false;
    static int64_t prev_char_timestamp = 0;

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

        /* Push decoded characters to WebUI SSE */
        decoded_char_t last = decoder_get_last_char();
        if (last.character != '\0' && last.timestamp_us != prev_char_timestamp) {
            webui_decoder_push_char(last.character, (uint8_t)decoder_get_wpm());
            if (last.character == ' ') {
                webui_decoder_push_word();
            }
            prev_char_timestamp = last.timestamp_us;
        }

        /* Tick text keyer */
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
