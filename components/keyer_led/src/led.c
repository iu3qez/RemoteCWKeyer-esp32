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

/* Maximum number of LEDs supported */
#define MAX_LEDS 16

/* WS2812B GRB color definitions */
#define COLOR_OFF       0x000000U
#define COLOR_RED       0x00FF00U   /* G=0, R=FF, B=0 */
#define COLOR_GREEN     0xFF0000U   /* G=FF, R=0, B=0 */
#define COLOR_BLUE      0x0000FFU   /* G=0, R=0, B=FF */
#define COLOR_ORANGE    0x80FF00U   /* G=80, R=FF, B=0 */
#define COLOR_YELLOW    0xA0FF00U   /* G=A0, R=FF, B=0 */
#define COLOR_MAGENTA   0x00FFFFU   /* G=0, R=FF, B=FF */

/* Animation timing constants */
#define BREATH_PERIOD_US    2000000  /* 2s full breath cycle */
#define FLASH_DURATION_US   100000   /* 100ms per flash */
#define FLASH_GAP_US        100000   /* 100ms between flashes */
#define RED_FLASH_US        500000   /* 500ms red flash */
#define AP_TOGGLE_US        500000   /* 500ms alternating */

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
    uint8_t pixel_buf[MAX_LEDS * 3];
    _Atomic led_state_t state;
    int64_t state_start_us;
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
 * @brief Apply brightness scaling to a 24-bit GRB color
 */
static uint32_t apply_brightness(uint32_t color, uint8_t brightness)
{
    if (brightness >= 100U) {
        return color;
    }

    uint8_t g = (uint8_t)((color >> 16) & 0xFFU);
    uint8_t r = (uint8_t)((color >> 8) & 0xFFU);
    uint8_t b = (uint8_t)(color & 0xFFU);

    g = (uint8_t)((g * brightness) / 100U);
    r = (uint8_t)((r * brightness) / 100U);
    b = (uint8_t)((b * brightness) / 100U);

    return ((uint32_t)g << 16) | ((uint32_t)r << 8) | (uint32_t)b;
}

/**
 * @brief Set all LEDs to a single color
 */
static void set_all_leds(uint32_t color)
{
    for (size_t i = 0; i < s_led.config.led_count; i++) {
        s_led.pixel_buf[i * 3U + 0U] = (uint8_t)((color >> 16) & 0xFFU); /* G */
        s_led.pixel_buf[i * 3U + 1U] = (uint8_t)((color >> 8) & 0xFFU);  /* R */
        s_led.pixel_buf[i * 3U + 2U] = (uint8_t)(color & 0xFFU);         /* B */
    }
}

/**
 * @brief Set a single LED to a color
 */
static void set_led(size_t index, uint32_t color)
{
    if (index >= s_led.config.led_count) {
        return;
    }

    s_led.pixel_buf[index * 3U + 0U] = (uint8_t)((color >> 16) & 0xFFU); /* G */
    s_led.pixel_buf[index * 3U + 1U] = (uint8_t)((color >> 8) & 0xFFU);  /* R */
    s_led.pixel_buf[index * 3U + 2U] = (uint8_t)(color & 0xFFU);         /* B */
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

/**
 * @brief Calculate breathing brightness (triangle wave)
 */
static uint8_t calculate_breathing(int64_t elapsed_us, uint8_t brightness)
{
    /* Triangle wave: 0 -> max -> 0 over BREATH_PERIOD_US */
    int64_t phase = elapsed_us % BREATH_PERIOD_US;
    uint32_t half_period = (uint32_t)(BREATH_PERIOD_US / 2);

    uint32_t value;
    if (phase < (int64_t)half_period) {
        /* Rising edge: 0 to brightness */
        value = ((uint32_t)phase * brightness) / half_period;
    } else {
        /* Falling edge: brightness to 0 */
        value = (((uint32_t)(BREATH_PERIOD_US - phase)) * brightness) / half_period;
    }

    return (uint8_t)value;
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

    if (config->led_count > MAX_LEDS) {
        ESP_LOGE(TAG, "led_count %u exceeds MAX_LEDS %u", config->led_count, MAX_LEDS);
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
    atomic_store_explicit(&s_led.state, LED_STATE_OFF, memory_order_relaxed);
    s_led.state_start_us = 0;

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

void led_set_state(led_state_t state)
{
    led_state_t old_state = atomic_load_explicit(&s_led.state, memory_order_relaxed);
    if (state != old_state) {
        atomic_store_explicit(&s_led.state, state, memory_order_relaxed);
        s_led.state_start_us = esp_timer_get_time();
        ESP_LOGI(TAG, "State: %d -> %d", old_state, state);
    }
}

led_state_t led_get_state(void)
{
    return atomic_load_explicit(&s_led.state, memory_order_relaxed);
}

void led_tick(int64_t now_us, bool dit, bool dah)
{
    if (!s_led.initialized) {
        return;
    }

    led_state_t current_state = atomic_load_explicit(&s_led.state, memory_order_relaxed);
    int64_t elapsed_us = now_us - s_led.state_start_us;
    uint8_t brightness = atomic_load_explicit(&s_led.brightness, memory_order_relaxed);
    uint8_t brightness_dim = atomic_load_explicit(&s_led.brightness_dim, memory_order_relaxed);

    switch (current_state) {
    case LED_STATE_OFF:
        set_all_leds(COLOR_OFF);
        break;

    case LED_STATE_BOOT:
    case LED_STATE_WIFI_CONNECTING: {
        /* Orange breathing */
        uint8_t breath = calculate_breathing(elapsed_us, brightness);
        uint32_t color = apply_brightness(COLOR_ORANGE, breath);
        set_all_leds(color);
        break;
    }

    case LED_STATE_WIFI_FAILED: {
        /* Red flash for 500ms, then auto-transition to DEGRADED */
        if (elapsed_us < RED_FLASH_US) {
            uint32_t color = apply_brightness(COLOR_RED, brightness);
            set_all_leds(color);
        } else {
            led_set_state(LED_STATE_DEGRADED);
            return; /* Will be rendered on next tick */
        }
        break;
    }

    case LED_STATE_DEGRADED: {
        /* Dim yellow steady */
        uint32_t color = apply_brightness(COLOR_YELLOW, brightness_dim);
        set_all_leds(color);
        break;
    }

    case LED_STATE_AP_MODE: {
        /* Alternating orange/blue every 500ms */
        int64_t phase = elapsed_us % (AP_TOGGLE_US * 2);
        uint32_t base_color = (phase < AP_TOGGLE_US) ? COLOR_ORANGE : COLOR_BLUE;
        uint32_t color = apply_brightness(base_color, brightness);
        set_all_leds(color);
        break;
    }

    case LED_STATE_CONNECTED: {
        /* 3 quick green flashes, then auto-transition to IDLE */
        int64_t flash_cycle = FLASH_DURATION_US + FLASH_GAP_US;
        int64_t flash_num = elapsed_us / flash_cycle;

        if (flash_num >= 3) {
            led_set_state(LED_STATE_IDLE);
            return; /* Will be rendered on next tick */
        }

        int64_t phase = elapsed_us % flash_cycle;
        if (phase < FLASH_DURATION_US) {
            uint32_t color = apply_brightness(COLOR_GREEN, brightness);
            set_all_leds(color);
        } else {
            set_all_leds(COLOR_OFF);
        }
        break;
    }

    case LED_STATE_IDLE: {
        /* Dim green steady with keying overlay */
        uint32_t base_color = apply_brightness(COLOR_GREEN, brightness_dim);
        set_all_leds(base_color);

        /* Keying overlay */
        if (dit && dah) {
            /* Squeeze: center LED magenta */
            size_t center = (size_t)s_led.config.led_count / 2U;
            uint32_t magenta = apply_brightness(COLOR_MAGENTA, brightness);
            set_led(center, magenta);
        } else if (dit) {
            /* DIT: left LEDs bright green */
            size_t center = (size_t)s_led.config.led_count / 2U;
            uint32_t bright_green = apply_brightness(COLOR_GREEN, brightness);
            for (size_t i = 0; i < center; i++) {
                set_led(i, bright_green);
            }
        } else if (dah) {
            /* DAH: right LEDs bright green */
            size_t center = (size_t)s_led.config.led_count / 2U;
            uint32_t bright_green = apply_brightness(COLOR_GREEN, brightness);
            for (size_t i = center + 1U; i < s_led.config.led_count; i++) {
                set_led(i, bright_green);
            }
        }
        break;
    }

    default:
        set_all_leds(COLOR_OFF);
        break;
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
