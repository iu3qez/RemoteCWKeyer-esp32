/**
 * @file hal_gpio.c
 * @brief GPIO HAL implementation with ISR-based paddle detection
 *
 * ISR + Blanking Strategy:
 * 1. GPIO interrupt triggers on falling edge (paddle press)
 * 2. ISR sets atomic flag and disables interrupt
 * 3. Blanking timer re-enables interrupt after blanking period
 * 4. RT task reads atomic flag to detect press with ~5µs latency
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

static volatile atomic_bool s_dit_pending = ATOMIC_VAR_INIT(false);
static volatile atomic_bool s_dah_pending = ATOMIC_VAR_INIT(false);

/* Blanking timers */
static esp_timer_handle_t s_dit_blanking_timer = NULL;
static esp_timer_handle_t s_dah_blanking_timer = NULL;

/* Diagnostic counters */
static volatile atomic_uint_fast32_t s_dit_isr_count = ATOMIC_VAR_INIT(0);
static volatile atomic_uint_fast32_t s_dah_isr_count = ATOMIC_VAR_INIT(0);
static volatile atomic_uint_fast32_t s_blanking_reject_count = ATOMIC_VAR_INIT(0);

/* Adaptive blanking state */
static uint32_t s_effective_blanking_us = 1500;  /* Default conservative */

/* ISR watchdog state - tracks when interrupts were disabled */
static volatile int64_t s_dit_disabled_at_us = 0;
static volatile int64_t s_dah_disabled_at_us = 0;
static volatile atomic_uint_fast32_t s_watchdog_recovery_count = ATOMIC_VAR_INIT(0);

/* ============================================================================
 * ISR Handlers (IRAM_ATTR - must be in internal RAM)
 * ============================================================================ */

/**
 * @brief DIT paddle ISR handler
 *
 * Called on falling edge (paddle pressed). Sets pending flag and
 * disables interrupt for blanking period to ignore bounce.
 */
static void IRAM_ATTR dit_isr_handler(void *arg) {
    (void)arg;

    /* Set pending flag for RT task */
    atomic_store_explicit(&s_dit_pending, true, memory_order_release);

    /* Increment trigger count */
    atomic_fetch_add_explicit(&s_dit_isr_count, 1, memory_order_relaxed);

    /* Record when we disabled the interrupt (for watchdog) */
    s_dit_disabled_at_us = esp_timer_get_time();

    /* Disable interrupt during blanking period */
    gpio_intr_disable((gpio_num_t)s_config.dit_pin);

    /* Start blanking timer to re-enable interrupt (using adaptive blanking) */
    esp_timer_start_once(s_dit_blanking_timer, (uint64_t)s_effective_blanking_us);
}

/**
 * @brief DAH paddle ISR handler
 */
static void IRAM_ATTR dah_isr_handler(void *arg) {
    (void)arg;

    atomic_store_explicit(&s_dah_pending, true, memory_order_release);
    atomic_fetch_add_explicit(&s_dah_isr_count, 1, memory_order_relaxed);

    /* Record when we disabled the interrupt (for watchdog) */
    s_dah_disabled_at_us = esp_timer_get_time();

    gpio_intr_disable((gpio_num_t)s_config.dah_pin);
    esp_timer_start_once(s_dah_blanking_timer, (uint64_t)s_effective_blanking_us);
}

/* ============================================================================
 * Blanking Timer Callbacks
 * ============================================================================ */

/**
 * @brief Re-enable DIT interrupt after blanking period
 */
static void dit_blanking_expired(void *arg) {
    (void)arg;
    s_dit_disabled_at_us = 0;  /* Mark as not disabled (for watchdog) */
    gpio_intr_enable((gpio_num_t)s_config.dit_pin);
}

/**
 * @brief Re-enable DAH interrupt after blanking period
 */
static void dah_blanking_expired(void *arg) {
    (void)arg;
    s_dah_disabled_at_us = 0;  /* Mark as not disabled (for watchdog) */
    gpio_intr_enable((gpio_num_t)s_config.dah_pin);
}

/* ============================================================================
 * ISR Initialization
 * ============================================================================ */

/**
 * @brief Initialize ISR-based paddle detection
 */
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
        /* ESP_ERR_INVALID_STATE means service already installed - that's OK */
        ESP_LOGE(TAG, "Failed to install GPIO ISR service: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Add ISR handlers for paddle pins */
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

/**
 * @brief Reset a GPIO pin to pure GPIO function
 *
 * Some GPIOs (especially GPIO4 on ESP32-S3) may be claimed by the bootloader
 * for JTAG or other functions. This helper forcibly disconnects the pin from
 * any peripheral and configures it as a simple GPIO input with pull-up.
 */
static void force_gpio_reset(gpio_num_t pin) {
    gpio_reset_pin(pin);
    /* gpio_reset_pin already resets to GPIO function, just configure */
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

    /*
     * CRITICAL: Force reset paddle pins before gpio_config().
     * On ESP32-S3, GPIO4 (and potentially others) may be held by the bootloader
     * for JTAG functions. A simple gpio_config() is not sufficient to reclaim them.
     */
    force_gpio_reset((gpio_num_t)config->dit_pin);
    force_gpio_reset((gpio_num_t)config->dah_pin);

    /* Determine interrupt type based on ISR config */
    gpio_int_type_t intr_type = GPIO_INTR_DISABLE;
    if (config->isr_blanking_us > 0) {
        /* Active low: falling edge = paddle pressed */
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
    ESP_LOGI(TAG, "DIT GPIO%d config: %s (pull_up=%d, intr=%d)",
             config->dit_pin, esp_err_to_name(err), dit_conf.pull_up_en, intr_type);

    /* Configure DAH input */
    gpio_config_t dah_conf = {
        .pin_bit_mask = (1ULL << config->dah_pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = intr_type,
    };
    err = gpio_config(&dah_conf);
    ESP_LOGI(TAG, "DAH GPIO%d config: %s (pull_up=%d, intr=%d)",
             config->dah_pin, esp_err_to_name(err), dah_conf.pull_up_en, intr_type);

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

    /* Ensure TX is off */
    hal_gpio_set_tx(false);

    /* Initialize ISR if blanking is configured */
    if (config->isr_blanking_us > 0) {
        err = init_isr();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "ISR init failed, falling back to polling only");
        }
    } else {
        ESP_LOGI(TAG, "ISR disabled (blanking=0), using polling only");
    }

    /* Read initial levels */
    int dit_level = gpio_get_level(config->dit_pin);
    int dah_level = gpio_get_level(config->dah_pin);
    ESP_LOGI(TAG, "Initial levels: DIT=%d, DAH=%d", dit_level, dah_level);
}

gpio_state_t hal_gpio_read_paddles(void) {
    int dit_level = gpio_get_level((gpio_num_t)s_config.dit_pin);
    int dah_level = gpio_get_level((gpio_num_t)s_config.dah_pin);

    /* Invert if active low: level=0 means pressed */
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

void hal_gpio_isr_get_stats(uint32_t *dit_triggers, uint32_t *dah_triggers,
                            uint32_t *blanking_rejects) {
    if (dit_triggers) {
        *dit_triggers = (uint32_t)atomic_load_explicit(&s_dit_isr_count, memory_order_relaxed);
    }
    if (dah_triggers) {
        *dah_triggers = (uint32_t)atomic_load_explicit(&s_dah_isr_count, memory_order_relaxed);
    }
    if (blanking_rejects) {
        *blanking_rejects = (uint32_t)atomic_load_explicit(&s_blanking_reject_count, memory_order_relaxed);
    }
}

void hal_gpio_update_blanking_for_wpm(uint32_t wpm) {
    const uint32_t BASE_BLANKING_US = 1500;
    const uint32_t MIN_BLANKING_US = 500;   /* Below this: bounce risk */

    if (wpm == 0) {
        wpm = 25;  /* Fallback to default */
    }

    /* Dit duration: 1,200,000 / WPM (in us) */
    uint32_t dit_us = 1200000 / wpm;

    /* QRQ worst case: inter-element = 50% dit */
    uint32_t inter_element_us = dit_us / 2;

    /* Blanking max = 40% inter-element (60% window for detection) */
    uint32_t max_blanking = (inter_element_us * 40) / 100;

    /* Start with base blanking */
    s_effective_blanking_us = BASE_BLANKING_US;

    /* Clamp to max allowed for this WPM */
    if (s_effective_blanking_us > max_blanking) {
        s_effective_blanking_us = max_blanking;
    }

    /* Enforce minimum to prevent bounce issues */
    if (s_effective_blanking_us < MIN_BLANKING_US) {
        s_effective_blanking_us = MIN_BLANKING_US;
    }

    ESP_LOGI(TAG, "Adaptive blanking: WPM=%lu -> blanking=%luus",
             (unsigned long)wpm, (unsigned long)s_effective_blanking_us);

    /* Warn user if reduced blanking for QRQ */
    if (wpm > 150 && s_effective_blanking_us < BASE_BLANKING_US) {
        ESP_LOGW(TAG, "QRQ mode: blanking reduced to %luus. "
                 "For optimal performance, consider hardware debounce.",
                 (unsigned long)s_effective_blanking_us);
    }
}

void hal_gpio_isr_watchdog(int64_t now_us) {
    if (!s_isr_enabled) {
        return;
    }

    /* Max time interrupt can be disabled: blanking + 1ms safety margin */
    const int64_t max_disabled_us = (int64_t)s_effective_blanking_us + 1000;

    /* Check DIT */
    int64_t dit_disabled = s_dit_disabled_at_us;
    if (dit_disabled > 0 && (now_us - dit_disabled) > max_disabled_us) {
        gpio_intr_enable((gpio_num_t)s_config.dit_pin);
        s_dit_disabled_at_us = 0;
        atomic_fetch_add_explicit(&s_watchdog_recovery_count, 1, memory_order_relaxed);
        /* Note: No logging in RT path - use counter for diagnostics */
    }

    /* Check DAH */
    int64_t dah_disabled = s_dah_disabled_at_us;
    if (dah_disabled > 0 && (now_us - dah_disabled) > max_disabled_us) {
        gpio_intr_enable((gpio_num_t)s_config.dah_pin);
        s_dah_disabled_at_us = 0;
        atomic_fetch_add_explicit(&s_watchdog_recovery_count, 1, memory_order_relaxed);
    }
}

uint32_t hal_gpio_get_watchdog_recoveries(void) {
    return (uint32_t)atomic_load_explicit(&s_watchdog_recovery_count, memory_order_relaxed);
}

uint32_t hal_gpio_get_effective_blanking_us(void) {
    return s_effective_blanking_us;
}

#else
/* ============================================================================
 * Host Stub Implementation
 * ============================================================================ */

#include <stdatomic.h>

static hal_gpio_config_t s_config = HAL_GPIO_CONFIG_DEFAULT;
static gpio_state_t s_paddle_state = {0};
static bool s_tx_state = false;

/* ISR simulation for host tests */
static atomic_bool s_dit_pending = ATOMIC_VAR_INIT(false);
static atomic_bool s_dah_pending = ATOMIC_VAR_INIT(false);
static uint32_t s_dit_isr_count = 0;
static uint32_t s_dah_isr_count = 0;

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

void hal_gpio_isr_get_stats(uint32_t *dit_triggers, uint32_t *dah_triggers,
                            uint32_t *blanking_rejects) {
    if (dit_triggers) *dit_triggers = s_dit_isr_count;
    if (dah_triggers) *dah_triggers = s_dah_isr_count;
    if (blanking_rejects) *blanking_rejects = 0;
}

/* Adaptive blanking state for host */
static uint32_t s_effective_blanking_us = 1500;
static uint32_t s_watchdog_recovery_count = 0;

void hal_gpio_update_blanking_for_wpm(uint32_t wpm) {
    const uint32_t BASE_BLANKING_US = 1500;
    const uint32_t MIN_BLANKING_US = 500;

    if (wpm == 0) {
        wpm = 25;
    }

    uint32_t dit_us = 1200000 / wpm;
    uint32_t inter_element_us = dit_us / 2;
    uint32_t max_blanking = (inter_element_us * 40) / 100;

    s_effective_blanking_us = BASE_BLANKING_US;
    if (s_effective_blanking_us > max_blanking) {
        s_effective_blanking_us = max_blanking;
    }
    if (s_effective_blanking_us < MIN_BLANKING_US) {
        s_effective_blanking_us = MIN_BLANKING_US;
    }
}

void hal_gpio_isr_watchdog(int64_t now_us) {
    (void)now_us;
    /* No-op in host build */
}

uint32_t hal_gpio_get_watchdog_recoveries(void) {
    return s_watchdog_recovery_count;
}

uint32_t hal_gpio_get_effective_blanking_us(void) {
    return s_effective_blanking_us;
}

/* Test helper to set paddle state */
void hal_gpio_test_set_paddles(bool dit, bool dah) {
    s_paddle_state = gpio_from_paddles(dit, dah);
}

/* Test helper to inject ISR press events */
void hal_gpio_test_inject_isr_press(bool dit, bool dah) {
    if (dit) {
        atomic_store(&s_dit_pending, true);
        s_dit_isr_count++;
    }
    if (dah) {
        atomic_store(&s_dah_pending, true);
        s_dah_isr_count++;
    }
}

#endif /* ESP_PLATFORM */
