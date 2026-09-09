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
#include "vpn.h"
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
/**
 * @brief The situation the LEDs show, from WiFi and from CWNet (#26)
 *
 * The rule is green means the operator's CW goes out (led_render.h), so
 * this answers exactly that. WiFi off in config is not a degradation: the
 * box was asked to be a local keyer and it is one, and the key line still
 * works, so it is green. WiFi asked for and missing is yellow. With the
 * link up, CWNet decides: disabled promises nothing remote and stays
 * green, otherwise the server has to be ready, TRANSMIT granted and the
 * key free or ours, which is what cwnet_socket_can_transmit() is.
 */
static led_situation_t led_situation_now(wifi_state_t ws) {
    switch (ws) {
        case WIFI_STATE_CONNECTING:
            return LED_SITUATION_STARTING;
        case WIFI_STATE_AP_MODE:
            return LED_SITUATION_SETUP;
        case WIFI_STATE_FAILED:
            return LED_SITUATION_LOCAL_ONLY;
        case WIFI_STATE_CONNECTED:
            break;
        case WIFI_STATE_DISABLED:
        default:
            return LED_SITUATION_ON_AIR;
    }

    if (cwnet_socket_get_state() == CWNET_SOCK_DISABLED) {
        return LED_SITUATION_ON_AIR;
    }
    return cwnet_socket_can_transmit() ? LED_SITUATION_ON_AIR : LED_SITUATION_OFF_AIR;
}

void bg_task(void *arg) {
    (void)arg;

    /* Note: All initialization (LED, WiFi, decoder, text_keyer) is done in main.c */

    /* Initialize timeline consumer (skip_threshold=0: never auto-skip) */
    best_effort_consumer_init(&s_timeline_consumer, &g_keying_stream, 0);
    s_timeline_initialized = true;

    /* Initialize CWNet client (reads config, connects if enabled) */
    cwnet_socket_init(&g_keying_stream);

    /* Log startup */
    int64_t now_us = esp_timer_get_time();
    RT_INFO(&g_bg_log_stream, now_us, "BG task started (text keyer ready)");

    uint32_t stats_counter = 0;
    wifi_state_t prev_wifi_state = WIFI_STATE_DISABLED;
    vpn_state_t prev_vpn_state = VPN_STATE_DISABLED;

    for (;;) {
        now_us = esp_timer_get_time();

        /* What the LEDs show, from WiFi and CWNet. Recomputed every tick:
         * the key can change hands without WiFi moving at all, and
         * led_set_situation() only restarts a clock on a real change. */
        if (led_is_initialized()) {
            wifi_state_t ws = wifi_get_state();

            led_set_situation(led_situation_now(ws));

            /* Detect state changes */
            if (ws != prev_wifi_state) {
                /* An event worth looking up for, in the colour it lands on */
                if (ws == WIFI_STATE_CONNECTED || ws == WIFI_STATE_FAILED) {
                    led_notify();
                }

                /* Log state change */
                if (ws == WIFI_STATE_CONNECTED) {
                    char ip_buf[16];
                    if (wifi_get_ip(ip_buf, sizeof(ip_buf))) {
                        RT_INFO(&g_bg_log_stream, now_us, "WiFi connected: %s", ip_buf);
                    }
                } else if (ws == WIFI_STATE_AP_MODE) {
                    RT_INFO(&g_bg_log_stream, now_us, "WiFi AP mode active");
                } else if (ws == WIFI_STATE_FAILED) {
                    RT_WARN(&g_bg_log_stream, now_us, "WiFi connection failed");
                }

                prev_wifi_state = ws;
            }

            /* Monitor VPN state changes */
            vpn_state_t vs = vpn_get_state();
            if (vs != prev_vpn_state) {
                switch (vs) {
                    case VPN_STATE_WAITING_WIFI:
                        RT_INFO(&g_bg_log_stream, now_us, "VPN: waiting for WiFi");
                        break;
                    case VPN_STATE_WAITING_TIME:
                        RT_INFO(&g_bg_log_stream, now_us, "VPN: syncing time (NTP)");
                        break;
                    case VPN_STATE_CONNECTING:
                        RT_INFO(&g_bg_log_stream, now_us, "VPN: WireGuard handshake");
                        break;
                    case VPN_STATE_CONNECTED:
                        RT_INFO(&g_bg_log_stream, now_us, "VPN: tunnel established");
                        break;
                    case VPN_STATE_FAILED:
                        RT_WARN(&g_bg_log_stream, now_us, "VPN: connection failed");
                        break;
                    default:
                        break;
                }
                prev_vpn_state = vs;
            }

            /* The paddle overlay is a second, independent read of the pins,
             * upstream of the debounce and the FSM: lit but silent means the
             * fault is in software, dark means the contact never closed. */
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
                    RT_INFO(&g_bg_log_stream, now_us, "CWNet: %s, latency=%"PRId32"ms, key %s (%s)",
                            cwnet_socket_state_str(cwnet_state), latency,
                            cwnet_client_key_holder_str(cwnet_socket_get_key_holder()),
                            cwnet_socket_get_key_holder_name());
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
