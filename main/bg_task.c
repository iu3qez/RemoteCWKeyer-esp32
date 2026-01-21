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
#include "consumer.h"
#include "rt_log.h"
#include "decoder.h"
#include "text_keyer.h"
#include "text_memory.h"
#include "led.h"
#include "wifi.h"
#include "hal_gpio.h"
#include "config.h"
#include "webui.h"
#include "cwnet_socket.h"

#include <stdio.h>

/* External globals */
extern keying_stream_t g_keying_stream;
extern fault_state_t g_fault_state;

/* ============================================================================
 * Timeline Consumer (best-effort, for WebUI visualization)
 * ============================================================================ */

static best_effort_consumer_t s_timeline_consumer;
static bool s_timeline_initialized = false;

/* Previous state for edge detection */
static gpio_state_t s_tl_prev_gpio = {0};
static uint8_t s_tl_prev_local_key = 0;

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

    /* Initialize timeline consumer (skip_threshold=0: never auto-skip) */
    best_effort_consumer_init(&s_timeline_consumer, &g_keying_stream, 0);
    s_timeline_initialized = true;

    /* Initialize CWNet client (reads config, connects if enabled) */
    cwnet_socket_init();

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

        /* Process CWNet socket (connection, send/receive) */
        cwnet_socket_process();

        /* Process decoder (reads from keying_stream) */
        decoder_process();

        /* Push decoded characters to WebUI */
        decoded_char_t ch;
        while ((ch = decoder_pop_char()).character != '\0') {
            webui_decoder_push_char(ch.character, (uint8_t)decoder_get_wpm());
            if (ch.character == ' ') {
                webui_decoder_push_word();
            }
        }

        /* Push current pattern if changed */
        {
            static char prev_pattern[16] = "";
            char pattern[16];
            decoder_get_current_pattern(pattern, sizeof(pattern));
            if (strcmp(pattern, prev_pattern) != 0) {
                webui_decoder_push_pattern(pattern);
                strncpy(prev_pattern, pattern, sizeof(prev_pattern) - 1);
                prev_pattern[sizeof(prev_pattern) - 1] = '\0';
            }
        }

        /* Process timeline events (only if WebSocket clients connected) */
        if (s_timeline_initialized && webui_get_ws_client_count() > 0) {
            stream_sample_t sample;
            while (best_effort_consumer_tick(&s_timeline_consumer, &sample)) {
                /* Skip silence markers */
                if (sample_is_silence(&sample)) {
                    continue;
                }

                char json[80];

                /* Check for DIT paddle edge */
                if (gpio_dit(sample.gpio) != gpio_dit(s_tl_prev_gpio)) {
                    snprintf(json, sizeof(json),
                        "{\"ts\":%lld,\"paddle\":0,\"state\":%d}",
                        (long long)(now_us / 1000),  /* Convert to ms */
                        gpio_dit(sample.gpio) ? 1 : 0);
                    webui_timeline_push("paddle", json);
                }

                /* Check for DAH paddle edge */
                if (gpio_dah(sample.gpio) != gpio_dah(s_tl_prev_gpio)) {
                    snprintf(json, sizeof(json),
                        "{\"ts\":%lld,\"paddle\":1,\"state\":%d}",
                        (long long)(now_us / 1000),
                        gpio_dah(sample.gpio) ? 1 : 0);
                    webui_timeline_push("paddle", json);
                }

                /* Check for keying output edge */
                if (sample.local_key != s_tl_prev_local_key) {
                    snprintf(json, sizeof(json),
                        "{\"ts\":%lld,\"state\":%d}",
                        (long long)(now_us / 1000),
                        sample.local_key ? 1 : 0);
                    webui_timeline_push("keying", json);

                    /* Forward key event to CWNet */
                    cwnet_socket_send_key_event(sample.local_key != 0);
                }

                /* Update previous state */
                s_tl_prev_gpio = sample.gpio;
                s_tl_prev_local_key = sample.local_key;
            }
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

            /* Log CWNet status if enabled */
            cwnet_socket_state_t cwnet_state = cwnet_socket_get_state();
            if (cwnet_state != CWNET_SOCK_DISABLED) {
                int32_t latency = cwnet_socket_get_latency_ms();
                if (latency >= 0) {
                    RT_INFO(&g_bg_log_stream, now_us, "CWNet: %s, latency=%"PRId32"ms",
                            cwnet_socket_state_str(cwnet_state), latency);
                } else {
                    RT_INFO(&g_bg_log_stream, now_us, "CWNet: %s",
                            cwnet_socket_state_str(cwnet_state));
                }
            }

            stats_counter = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(10));  /* 100Hz - adequate for LED animations */
    }
}
