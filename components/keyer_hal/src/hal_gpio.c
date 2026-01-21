/**
 * @file hal_gpio.c
 * @brief GPIO HAL implementation with ISR-based paddle detection
 *
 * ISR + Blanking Strategy (ISR-safe design):
 * 1. GPIO interrupt triggers on falling edge (paddle press)
 * 2. ISR sets atomic flags and disables interrupt (ISR-safe ops only)
 * 3. RT task (via hal_gpio_isr_tick) starts blanking timer (task context)
 * 4. Blanking timer callback re-enables interrupt after blanking period
 * 5. RT task reads atomic flag to detect press
 *
 * Key insight: esp_timer_start_once() is NOT ISR-safe (uses spinlocks).
 * Solution: ISR sets a flag, RT task starts the timer from task context.
 * This adds ~1ms latency to blanking start but avoids crashes.
 */

#include "hal_gpio.h"

#ifdef ESP_PLATFORM
/* ESP-IDF target build */
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdatomic.h>

static const char *TAG = "hal_gpio";
static hal_gpio_config_t s_config = HAL_GPIO_CONFIG_DEFAULT;
static bool s_tx_state = false;
static bool s_isr_enabled = false;

/* ============================================================================
 * ISR State (atomic communication with RT task)
 * ============================================================================ */

/* Pending press flags (set by ISR, consumed by RT task) */
static volatile atomic_bool s_dit_pending = ATOMIC_VAR_INIT(false);
static volatile atomic_bool s_dah_pending = ATOMIC_VAR_INIT(false);

/* Flags to signal RT task to start blanking timers (ISR-safe handoff) */
static volatile atomic_bool s_dit_needs_blanking = ATOMIC_VAR_INIT(false);
static volatile atomic_bool s_dah_needs_blanking = ATOMIC_VAR_INIT(false);

/* Timestamps when interrupts were disabled (for watchdog) */
static volatile int64_t s_dit_disabled_at_us = 0;
static volatile int64_t s_dah_disabled_at_us = 0;

/* Blanking timers */
static esp_timer_handle_t s_dit_blanking_timer = NULL;
static esp_timer_handle_t s_dah_blanking_timer = NULL;

/* ============================================================================
 * ISR Handlers (IRAM_ATTR - must be in internal RAM)
 *
 * These handlers use ONLY ISR-safe operations:
 * - atomic_store (lock-free)
 * - esp_timer_get_time (IRAM-safe, no locks)
 * - gpio_intr_disable (IRAM-safe)
 *
 * NOT called from here (not ISR-safe):
 * - esp_timer_start_once (uses spinlocks!)
 * - ESP_LOGx (uses locks)
 * ============================================================================ */

static void IRAM_ATTR dit_isr_handler(void *arg) {
    (void)arg;

    /* Set pending flag for RT task */
    atomic_store_explicit(&s_dit_pending, true, memory_order_release);

    /* Disable interrupt to prevent bounce storms */
    gpio_intr_disable((gpio_num_t)s_config.dit_pin);

    /* Record when we disabled (for watchdog) */
    s_dit_disabled_at_us = esp_timer_get_time();

    /* Signal RT task to start blanking timer */
    atomic_store_explicit(&s_dit_needs_blanking, true, memory_order_release);
}

static void IRAM_ATTR dah_isr_handler(void *arg) {
    (void)arg;

    atomic_store_explicit(&s_dah_pending, true, memory_order_release);
    gpio_intr_disable((gpio_num_t)s_config.dah_pin);
    s_dah_disabled_at_us = esp_timer_get_time();
    atomic_store_explicit(&s_dah_needs_blanking, true, memory_order_release);
}

/* ============================================================================
 * Blanking Timer Callbacks (run in esp_timer task context)
 * ============================================================================ */

static void dit_blanking_expired(void *arg) {
    (void)arg;
    s_dit_disabled_at_us = 0;
    gpio_intr_enable((gpio_num_t)s_config.dit_pin);
}

static void dah_blanking_expired(void *arg) {
    (void)arg;
    s_dah_disabled_at_us = 0;
    gpio_intr_enable((gpio_num_t)s_config.dah_pin);
}

/* ============================================================================
 * ISR Initialization
 * ============================================================================ */

static esp_err_t init_isr(void) {
    esp_err_t ret;

    /* Create blanking timers */
    esp_timer_create_args_t dit_timer_args = {
        .callback = dit_blanking_expired,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "dit_blank",
    };
    ret = esp_timer_create(&dit_timer_args, &s_dit_blanking_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create DIT blanking timer: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_timer_create_args_t dah_timer_args = {
        .callback = dah_blanking_expired,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "dah_blank",
    };
    ret = esp_timer_create(&dah_timer_args, &s_dah_blanking_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create DAH blanking timer: %s", esp_err_to_name(ret));
        esp_timer_delete(s_dit_blanking_timer);
        s_dit_blanking_timer = NULL;
        return ret;
    }

    /* Install GPIO ISR service */
    ret = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to install GPIO ISR service: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Add ISR handlers */
    ret = gpio_isr_handler_add((gpio_num_t)s_config.dit_pin, dit_isr_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add DIT ISR handler: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = gpio_isr_handler_add((gpio_num_t)s_config.dah_pin, dah_isr_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add DAH ISR handler: %s", esp_err_to_name(ret));
        gpio_isr_handler_remove((gpio_num_t)s_config.dit_pin);
        return ret;
    }

    s_isr_enabled = true;
    ESP_LOGI(TAG, "ISR paddle detection enabled (blanking=%luus)",
             (unsigned long)s_config.isr_blanking_us);

    return ESP_OK;
}

/* ============================================================================
 * GPIO Reset Helper
 * ============================================================================ */

static void force_gpio_reset(gpio_num_t pin) {
    gpio_reset_pin(pin);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    gpio_iomux_out(pin, 1, false);
    gpio_iomux_in(pin, 0x100);
#pragma GCC diagnostic pop
    gpio_set_direction(pin, GPIO_MODE_INPUT);
    gpio_set_pull_mode(pin, GPIO_PULLUP_ONLY);
}

/* ============================================================================
 * Public API
 * ============================================================================ */

void hal_gpio_init(const hal_gpio_config_t *config) {
    s_config = *config;

    ESP_LOGI(TAG, "Configuring GPIO: DIT=%d, DAH=%d, TX=%d, active_low=%d, isr_blanking=%luus",
             config->dit_pin, config->dah_pin, config->tx_pin, config->active_low,
             (unsigned long)config->isr_blanking_us);

    /* Force reset paddle pins */
    force_gpio_reset((gpio_num_t)config->dit_pin);
    force_gpio_reset((gpio_num_t)config->dah_pin);

    /* Determine interrupt type */
    gpio_int_type_t intr_type = GPIO_INTR_DISABLE;
    if (config->isr_blanking_us > 0) {
        intr_type = config->active_low ? GPIO_INTR_NEGEDGE : GPIO_INTR_POSEDGE;
    }

    /* Configure DIT input */
    gpio_config_t dit_conf = {
        .pin_bit_mask = (1ULL << config->dit_pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = intr_type,
    };
    esp_err_t err = gpio_config(&dit_conf);
    ESP_LOGI(TAG, "DIT GPIO%d config: %s", config->dit_pin, esp_err_to_name(err));

    /* Configure DAH input */
    gpio_config_t dah_conf = {
        .pin_bit_mask = (1ULL << config->dah_pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = intr_type,
    };
    err = gpio_config(&dah_conf);
    ESP_LOGI(TAG, "DAH GPIO%d config: %s", config->dah_pin, esp_err_to_name(err));

    /* Configure TX output */
    gpio_config_t tx_conf = {
        .pin_bit_mask = (1ULL << config->tx_pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&tx_conf);
    ESP_LOGI(TAG, "TX GPIO%d config: %s", config->tx_pin, esp_err_to_name(err));

    hal_gpio_set_tx(false);

    /* Initialize ISR if configured */
    if (config->isr_blanking_us > 0) {
        err = init_isr();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "ISR init failed, using polling only");
        }
    } else {
        ESP_LOGI(TAG, "ISR disabled, using polling only");
    }

    int dit_level = gpio_get_level(config->dit_pin);
    int dah_level = gpio_get_level(config->dah_pin);
    ESP_LOGI(TAG, "Initial levels: DIT=%d, DAH=%d", dit_level, dah_level);
}

gpio_state_t hal_gpio_read_paddles(void) {
    int dit_level = gpio_get_level((gpio_num_t)s_config.dit_pin);
    int dah_level = gpio_get_level((gpio_num_t)s_config.dah_pin);

    bool dit_pressed = s_config.active_low ? (dit_level == 0) : (dit_level != 0);
    bool dah_pressed = s_config.active_low ? (dah_level == 0) : (dah_level != 0);

    return gpio_from_paddles(dit_pressed, dah_pressed);
}

void hal_gpio_set_tx(bool on) {
    s_tx_state = on;
    uint32_t level = s_config.tx_active_high ? (on ? 1U : 0U) : (on ? 0U : 1U);
    gpio_set_level(s_config.tx_pin, level);
}

bool hal_gpio_get_tx(void) {
    return s_tx_state;
}

hal_gpio_config_t hal_gpio_get_config(void) {
    return s_config;
}

bool hal_gpio_consume_dit_press(void) {
    return atomic_exchange_explicit(&s_dit_pending, false, memory_order_acquire);
}

bool hal_gpio_consume_dah_press(void) {
    return atomic_exchange_explicit(&s_dah_pending, false, memory_order_acquire);
}

bool hal_gpio_isr_enabled(void) {
    return s_isr_enabled;
}

void hal_gpio_isr_tick(int64_t now_us) {
    if (!s_isr_enabled) {
        return;
    }

    /*
     * Start blanking timers requested by ISR (task context = safe).
     * This is the ISR-safe handoff: ISR sets flag, RT task starts timer.
     */
    if (atomic_exchange_explicit(&s_dit_needs_blanking, false, memory_order_acquire)) {
        esp_timer_start_once(s_dit_blanking_timer, (uint64_t)s_config.isr_blanking_us);
    }
    if (atomic_exchange_explicit(&s_dah_needs_blanking, false, memory_order_acquire)) {
        esp_timer_start_once(s_dah_blanking_timer, (uint64_t)s_config.isr_blanking_us);
    }

    /* Watchdog: recover if interrupt stuck disabled too long */
    const int64_t max_disabled_us = (int64_t)s_config.isr_blanking_us + 2000;

    int64_t dit_disabled = s_dit_disabled_at_us;
    if (dit_disabled > 0 && (now_us - dit_disabled) > max_disabled_us) {
        gpio_intr_enable((gpio_num_t)s_config.dit_pin);
        s_dit_disabled_at_us = 0;
    }

    int64_t dah_disabled = s_dah_disabled_at_us;
    if (dah_disabled > 0 && (now_us - dah_disabled) > max_disabled_us) {
        gpio_intr_enable((gpio_num_t)s_config.dah_pin);
        s_dah_disabled_at_us = 0;
    }
}

#else
/* ============================================================================
 * Host Stub Implementation
 * ============================================================================ */

#include <stdatomic.h>

static hal_gpio_config_t s_config = HAL_GPIO_CONFIG_DEFAULT;
static gpio_state_t s_paddle_state = {0};
static bool s_tx_state = false;
static atomic_bool s_dit_pending = ATOMIC_VAR_INIT(false);
static atomic_bool s_dah_pending = ATOMIC_VAR_INIT(false);

void hal_gpio_init(const hal_gpio_config_t *config) {
    s_config = *config;
}

gpio_state_t hal_gpio_read_paddles(void) {
    return s_paddle_state;
}

void hal_gpio_set_tx(bool on) {
    s_tx_state = on;
}

bool hal_gpio_get_tx(void) {
    return s_tx_state;
}

hal_gpio_config_t hal_gpio_get_config(void) {
    return s_config;
}

bool hal_gpio_consume_dit_press(void) {
    return atomic_exchange(&s_dit_pending, false);
}

bool hal_gpio_consume_dah_press(void) {
    return atomic_exchange(&s_dah_pending, false);
}

bool hal_gpio_isr_enabled(void) {
    return s_config.isr_blanking_us > 0;
}

void hal_gpio_isr_tick(int64_t now_us) {
    (void)now_us;
}

/* Test helpers */
void hal_gpio_test_set_paddles(bool dit, bool dah) {
    s_paddle_state = gpio_from_paddles(dit, dah);
}

void hal_gpio_test_inject_isr_press(bool dit, bool dah) {
    if (dit) atomic_store(&s_dit_pending, true);
    if (dah) atomic_store(&s_dah_pending, true);
}

#endif /* ESP_PLATFORM */
