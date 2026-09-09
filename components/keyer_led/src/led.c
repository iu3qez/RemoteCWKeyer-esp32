/**
 * @file led.c
 * @brief WS2812B RGB LED driver implementation
 */

#include "led.h"
#include "driver/rmt_tx.h"
#include "esp_timer.h"
#include <string.h>
#include <stdatomic.h>
#include "esp_log.h"

static const char *TAG = "led";

/* Colours, animation clocks and the render itself: led_render.h */

/* RMT configuration */
#define RMT_RESOLUTION_HZ   10000000 /* 10MHz resolution (100ns) */
#define WS2812B_T0H_TICKS   4        /* 0: 400ns high */
#define WS2812B_T0L_TICKS   9        /* 0: 900ns low */
#define WS2812B_T1H_TICKS   8        /* 1: 800ns high */
#define WS2812B_T1L_TICKS   5        /* 1: 500ns low */
#define WS2812B_RESET_TICKS 100      /* Reset: 10µs low */

/**
 * @brief LED strip encoder state
 */
typedef struct {
    rmt_encoder_t base;
    rmt_encoder_t *bytes_encoder;
    rmt_encoder_t *copy_encoder;
    rmt_symbol_word_t reset_code;
    int state;
} led_strip_encoder_t;

/**
 * @brief LED driver state
 */
static struct {
    bool initialized;
    led_config_t config;
    rmt_channel_handle_t rmt_channel;
    rmt_encoder_handle_t encoder;
    uint8_t pixel_buf[LED_MAX_COUNT * 3];
    _Atomic led_situation_t situation;
    int64_t situation_start_us;
    int64_t notify_start_us;
    bool notify_active;
    _Atomic uint8_t brightness;
    _Atomic uint8_t brightness_dim;
} s_led;

/* Forward declarations */
static size_t led_strip_encode(rmt_encoder_t *encoder, rmt_channel_handle_t channel,
                                const void *primary_data, size_t data_size,
                                rmt_encode_state_t *ret_state);
static esp_err_t led_strip_del(rmt_encoder_t *encoder);
static esp_err_t led_strip_reset(rmt_encoder_t *encoder);

/**
 * @brief Create LED strip encoder
 */
static esp_err_t led_strip_encoder_new(rmt_encoder_handle_t *ret_encoder)
{
    if (ret_encoder == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    led_strip_encoder_t *encoder = calloc(1, sizeof(led_strip_encoder_t));
    if (encoder == NULL) {
        return ESP_ERR_NO_MEM;
    }

    encoder->base.encode = led_strip_encode;
    encoder->base.del = led_strip_del;
    encoder->base.reset = led_strip_reset;

    /* Create bytes encoder for WS2812B data */
    rmt_bytes_encoder_config_t bytes_config = {
        .bit0 = {
            .level0 = 1,
            .duration0 = WS2812B_T0H_TICKS,
            .level1 = 0,
            .duration1 = WS2812B_T0L_TICKS,
        },
        .bit1 = {
            .level0 = 1,
            .duration0 = WS2812B_T1H_TICKS,
            .level1 = 0,
            .duration1 = WS2812B_T1L_TICKS,
        },
        .flags.msb_first = 1,
    };

    esp_err_t ret = rmt_new_bytes_encoder(&bytes_config, &encoder->bytes_encoder);
    if (ret != ESP_OK) {
        free(encoder);
        return ret;
    }

    /* Create copy encoder for reset code */
    rmt_copy_encoder_config_t copy_config = {};
    ret = rmt_new_copy_encoder(&copy_config, &encoder->copy_encoder);
    if (ret != ESP_OK) {
        rmt_del_encoder(encoder->bytes_encoder);
        free(encoder);
        return ret;
    }

    /* Reset code: low for WS2812B_RESET_TICKS */
    encoder->reset_code.level0 = 0;
    encoder->reset_code.duration0 = WS2812B_RESET_TICKS;
    encoder->reset_code.level1 = 0;
    encoder->reset_code.duration1 = 0;

    *ret_encoder = &encoder->base;
    return ESP_OK;
}

/**
 * @brief Encode LED strip data
 */
static size_t led_strip_encode(rmt_encoder_t *encoder, rmt_channel_handle_t channel,
                                const void *primary_data, size_t data_size,
                                rmt_encode_state_t *ret_state)
{
    led_strip_encoder_t *led_encoder = __containerof(encoder, led_strip_encoder_t, base);
    rmt_encode_state_t session_state = RMT_ENCODING_RESET;
    rmt_encode_state_t state = RMT_ENCODING_RESET;
    size_t encoded_symbols = 0;

    switch (led_encoder->state) {
    case 0: /* Send RGB data */
        encoded_symbols += led_encoder->bytes_encoder->encode(
            led_encoder->bytes_encoder, channel, primary_data, data_size, &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            led_encoder->state = 1; /* Go to reset code */
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out;
        }
        /* Fall through */
        __attribute__((fallthrough));
    case 1: /* Send reset code */
        encoded_symbols += led_encoder->copy_encoder->encode(
            led_encoder->copy_encoder, channel, &led_encoder->reset_code,
            sizeof(led_encoder->reset_code), &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            led_encoder->state = RMT_ENCODING_RESET;
            state |= RMT_ENCODING_COMPLETE;
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
        }
        break;
    }

out:
    *ret_state = state;
    return encoded_symbols;
}

/**
 * @brief Delete encoder
 */
static esp_err_t led_strip_del(rmt_encoder_t *encoder)
{
    led_strip_encoder_t *led_encoder = __containerof(encoder, led_strip_encoder_t, base);
    rmt_del_encoder(led_encoder->bytes_encoder);
    rmt_del_encoder(led_encoder->copy_encoder);
    free(led_encoder);
    return ESP_OK;
}

/**
 * @brief Reset encoder state
 */
static esp_err_t led_strip_reset(rmt_encoder_t *encoder)
{
    led_strip_encoder_t *led_encoder = __containerof(encoder, led_strip_encoder_t, base);
    rmt_encoder_reset(led_encoder->bytes_encoder);
    rmt_encoder_reset(led_encoder->copy_encoder);
    led_encoder->state = 0;
    return ESP_OK;
}

/**
 * @brief Transmit pixel buffer to LEDs
 */
static void transmit_leds(void)
{
    if (!s_led.initialized) {
        return;
    }

    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
    };

    size_t buf_size = (size_t)s_led.config.led_count * 3U;
    esp_err_t ret = rmt_transmit(s_led.rmt_channel, s_led.encoder,
                                  s_led.pixel_buf, buf_size, &tx_config);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "rmt_transmit failed: %s", esp_err_to_name(ret));
    }
}

esp_err_t led_init(const led_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_led.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    if (config->led_count > LED_MAX_COUNT) {
        ESP_LOGE(TAG, "led_count %u exceeds LED_MAX_COUNT %u", config->led_count, LED_MAX_COUNT);
        return ESP_ERR_INVALID_ARG;
    }

    if (config->led_count == 0U) {
        ESP_LOGE(TAG, "led_count cannot be zero");
        return ESP_ERR_INVALID_ARG;
    }

    /* Save configuration */
    s_led.config = *config;
    atomic_store_explicit(&s_led.brightness, config->brightness, memory_order_relaxed);
    atomic_store_explicit(&s_led.brightness_dim, config->brightness_dim, memory_order_relaxed);
    atomic_store_explicit(&s_led.situation, LED_SITUATION_STARTING, memory_order_relaxed);
    s_led.situation_start_us = 0;
    s_led.notify_start_us = 0;
    s_led.notify_active = false;

    /* Configure RMT TX channel */
    rmt_tx_channel_config_t tx_config = {
        .gpio_num = (gpio_num_t)config->gpio_data,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RMT_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
        .flags.invert_out = false,
        .flags.with_dma = false,
    };

    esp_err_t ret = rmt_new_tx_channel(&tx_config, &s_led.rmt_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_tx_channel failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Create LED strip encoder */
    ret = led_strip_encoder_new(&s_led.encoder);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "led_strip_encoder_new failed: %s", esp_err_to_name(ret));
        rmt_del_channel(s_led.rmt_channel);
        return ret;
    }

    /* Enable RMT channel */
    ret = rmt_enable(s_led.rmt_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "rmt_enable failed: %s", esp_err_to_name(ret));
        rmt_del_encoder(s_led.encoder);
        rmt_del_channel(s_led.rmt_channel);
        return ret;
    }

    /* Initialize all LEDs off */
    memset(s_led.pixel_buf, 0, sizeof(s_led.pixel_buf));
    transmit_leds();

    s_led.initialized = true;
    ESP_LOGI(TAG, "Initialized: gpio=%u, count=%u, brightness=%u/%u",
             config->gpio_data, config->led_count, config->brightness, config->brightness_dim);

    return ESP_OK;
}

void led_deinit(void)
{
    if (!s_led.initialized) {
        return;
    }

    /* Turn off all LEDs */
    memset(s_led.pixel_buf, 0, sizeof(s_led.pixel_buf));
    transmit_leds();

    /* Disable and delete RMT resources */
    rmt_disable(s_led.rmt_channel);
    rmt_del_encoder(s_led.encoder);
    rmt_del_channel(s_led.rmt_channel);

    s_led.initialized = false;
    ESP_LOGI(TAG, "Deinitialized");
}

void led_set_situation(led_situation_t situation)
{
    led_situation_t previous = atomic_load_explicit(&s_led.situation, memory_order_relaxed);
    if (situation != previous) {
        atomic_store_explicit(&s_led.situation, situation, memory_order_relaxed);
        s_led.situation_start_us = esp_timer_get_time();
        ESP_LOGI(TAG, "Situation: %d -> %d", (int)previous, (int)situation);
    }
}

led_situation_t led_get_situation(void)
{
    return atomic_load_explicit(&s_led.situation, memory_order_relaxed);
}

void led_notify(void)
{
    s_led.notify_start_us = esp_timer_get_time();
    s_led.notify_active = true;
}

void led_tick(int64_t now_us, bool dit, bool dah)
{
    if (!s_led.initialized) {
        return;
    }

    int64_t notify_elapsed = -1;
    if (s_led.notify_active) {
        notify_elapsed = now_us - s_led.notify_start_us;
        if (notify_elapsed >= LED_NOTIFY_TOTAL_US) {
            s_led.notify_active = false;
            notify_elapsed = -1;
        }
    }

    const led_scene_t scene = {
        .situation = atomic_load_explicit(&s_led.situation, memory_order_relaxed),
        .situation_elapsed_us = now_us - s_led.situation_start_us,
        .notify_elapsed_us = notify_elapsed,
        .dit = dit,
        .dah = dah,
        .count = s_led.config.led_count,
        .brightness = atomic_load_explicit(&s_led.brightness, memory_order_relaxed),
        .brightness_dim = atomic_load_explicit(&s_led.brightness_dim, memory_order_relaxed),
    };

    led_frame_t frame;
    led_render(&scene, &frame);

    for (uint8_t i = 0; i < frame.count; i++) {
        s_led.pixel_buf[i * 3U + 0U] = (uint8_t)((frame.color[i] >> 16) & 0xFFU); /* R */
        s_led.pixel_buf[i * 3U + 1U] = (uint8_t)((frame.color[i] >> 8) & 0xFFU);  /* G */
        s_led.pixel_buf[i * 3U + 2U] = (uint8_t)(frame.color[i] & 0xFFU);         /* B */
    }

    transmit_leds();
}

void led_set_brightness(uint8_t brightness, uint8_t brightness_dim)
{
    atomic_store_explicit(&s_led.brightness, brightness, memory_order_relaxed);
    atomic_store_explicit(&s_led.brightness_dim, brightness_dim, memory_order_relaxed);
}

bool led_is_initialized(void)
{
    return s_led.initialized;
}
