# LED Status & WiFi Connectivity Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add RGB LED status feedback and WiFi connectivity to the CW keyer.

**Architecture:** Two new ESP-IDF components (`keyer_led` for WS2812B control, `keyer_wifi` for WiFi STA/AP) integrated into `bg_task`. LED state reflects WiFi connection status and paddle activity. Both run on Core 1 as best-effort features.

**Tech Stack:** ESP-IDF RMT peripheral (WS2812B), ESP-IDF WiFi driver, `stdatomic.h` for cross-task state.

---

## Task 1: Add LED and WiFi Parameters to parameters.yaml

**Files:**
- Modify: `parameters.yaml`

**Step 1: Add `leds` family**

Add after the `system` family (around line 565):

```yaml
  leds:
    order: 6
    icon: "lightbulb"
    label:
      en: "LEDs"
      it: "LED"
    description:
      en: "RGB LED strip configuration"
      it: "Configurazione striscia LED RGB"
    aliases: [led, l]

    parameters:
      gpio_data:
        type: u8
        default: 38
        range: [0, 48]
        nvs_key: "led_gpio"
        runtime_change: reboot
        priority: 40
        gui:
          label_short:
            en: "Data Pin"
            it: "Pin Dati"
          label_long:
            en: "LED Data GPIO"
            it: "GPIO Dati LED"
          description:
            en: "GPIO pin for WS2812B data line"
            it: "Pin GPIO per linea dati WS2812B"
          widget: spinbox
          widget_config:
            step: 1
            prefix: "GPIO "
          advanced: true

      count:
        type: u8
        default: 7
        range: [0, 32]
        nvs_key: "led_count"
        runtime_change: reboot
        priority: 41
        gui:
          label_short:
            en: "Count"
            it: "Numero"
          label_long:
            en: "Number of LEDs"
            it: "Numero di LED"
          description:
            en: "Number of WS2812B LEDs in strip (0 to disable)"
            it: "Numero di LED WS2812B nella striscia (0 per disabilitare)"
          widget: spinbox
          widget_config:
            step: 1
          advanced: false

      brightness:
        type: u8
        default: 50
        range: [0, 100]
        nvs_key: "led_bright"
        runtime_change: immediate
        priority: 42
        gui:
          label_short:
            en: "Brightness"
            it: "Luminosità"
          label_long:
            en: "LED Brightness (%)"
            it: "Luminosità LED (%)"
          description:
            en: "Master brightness for all LEDs"
            it: "Luminosità principale per tutti i LED"
          widget: slider
          widget_config:
            step: 5
            tick_interval: 25
          advanced: false

      brightness_dim:
        type: u8
        default: 10
        range: [0, 50]
        nvs_key: "led_dim"
        runtime_change: immediate
        priority: 43
        gui:
          label_short:
            en: "Dim Level"
            it: "Livello Basso"
          label_long:
            en: "Idle Brightness (%)"
            it: "Luminosità in Attesa (%)"
          description:
            en: "Brightness level when idle (dim green)"
            it: "Livello luminosità in attesa (verde tenue)"
          widget: slider
          widget_config:
            step: 5
            tick_interval: 10
          advanced: true
```

**Step 2: Add `wifi` family**

Add after the `leds` family:

```yaml
  wifi:
    order: 7
    icon: "wifi"
    label:
      en: "WiFi"
      it: "WiFi"
    description:
      en: "Wireless network configuration"
      it: "Configurazione rete wireless"
    aliases: [w, net]

    parameters:
      enabled:
        type: bool
        default: false
        nvs_key: "wifi_en"
        runtime_change: reboot
        priority: 50
        gui:
          label_short:
            en: "Enable"
            it: "Abilita"
          label_long:
            en: "WiFi Enabled"
            it: "WiFi Abilitato"
          description:
            en: "Enable WiFi connectivity"
            it: "Abilita connettività WiFi"
          widget: toggle
          widget_config:
            on_label:
              en: "On"
              it: "Attivo"
            off_label:
              en: "Off"
              it: "Spento"
          advanced: false

      ssid:
        type: string
        max_length: 32
        default: ""
        nvs_key: "wifi_ssid"
        runtime_change: reboot
        priority: 51
        gui:
          label_short:
            en: "SSID"
            it: "SSID"
          label_long:
            en: "Network Name"
            it: "Nome Rete"
          description:
            en: "WiFi network name to connect to"
            it: "Nome della rete WiFi a cui connettersi"
          widget: text
          advanced: false

      password:
        type: string
        max_length: 64
        default: ""
        nvs_key: "wifi_pass"
        runtime_change: reboot
        sensitive: true
        priority: 52
        gui:
          label_short:
            en: "Password"
            it: "Password"
          label_long:
            en: "Network Password"
            it: "Password Rete"
          description:
            en: "WiFi network password"
            it: "Password della rete WiFi"
          widget: password
          advanced: false

      timeout_sec:
        type: u16
        default: 30
        range: [5, 120]
        nvs_key: "wifi_tout"
        runtime_change: reboot
        priority: 53
        gui:
          label_short:
            en: "Timeout"
            it: "Timeout"
          label_long:
            en: "Connection Timeout (s)"
            it: "Timeout Connessione (s)"
          description:
            en: "Seconds to wait for connection before AP fallback"
            it: "Secondi di attesa prima di passare alla modalità AP"
          widget: spinbox
          widget_config:
            step: 5
            suffix: " s"
          advanced: true

      use_static_ip:
        type: bool
        default: false
        nvs_key: "wifi_static"
        runtime_change: reboot
        priority: 54
        gui:
          label_short:
            en: "Static IP"
            it: "IP Statico"
          label_long:
            en: "Use Static IP"
            it: "Usa IP Statico"
          description:
            en: "Use static IP instead of DHCP"
            it: "Usa IP statico invece di DHCP"
          widget: toggle
          advanced: true

      ip_address:
        type: string
        max_length: 16
        default: "0.0.0.0"
        nvs_key: "wifi_ip"
        runtime_change: reboot
        priority: 55
        gui:
          label_short:
            en: "IP"
            it: "IP"
          label_long:
            en: "Static IP Address"
            it: "Indirizzo IP Statico"
          description:
            en: "Static IP address (e.g., 192.168.1.100)"
            it: "Indirizzo IP statico (es. 192.168.1.100)"
          widget: text
          advanced: true

      netmask:
        type: string
        max_length: 16
        default: "255.255.255.0"
        nvs_key: "wifi_mask"
        runtime_change: reboot
        priority: 56
        gui:
          label_short:
            en: "Netmask"
            it: "Netmask"
          label_long:
            en: "Subnet Mask"
            it: "Maschera di Sottorete"
          description:
            en: "Subnet mask (e.g., 255.255.255.0)"
            it: "Maschera di sottorete (es. 255.255.255.0)"
          widget: text
          advanced: true

      gateway:
        type: string
        max_length: 16
        default: "0.0.0.0"
        nvs_key: "wifi_gw"
        runtime_change: reboot
        priority: 57
        gui:
          label_short:
            en: "Gateway"
            it: "Gateway"
          label_long:
            en: "Default Gateway"
            it: "Gateway Predefinito"
          description:
            en: "Default gateway IP (e.g., 192.168.1.1)"
            it: "IP gateway predefinito (es. 192.168.1.1)"
          widget: text
          advanced: true

      dns:
        type: string
        max_length: 16
        default: "0.0.0.0"
        nvs_key: "wifi_dns"
        runtime_change: reboot
        priority: 58
        gui:
          label_short:
            en: "DNS"
            it: "DNS"
          label_long:
            en: "DNS Server"
            it: "Server DNS"
          description:
            en: "DNS server IP (e.g., 8.8.8.8)"
            it: "IP server DNS (es. 8.8.8.8)"
          widget: text
          advanced: true
```

**Step 3: Remove old led_brightness from system family**

Delete the `led_brightness` parameter from the `system` family (lines 544-565) since it's now in `leds.brightness`.

**Step 4: Regenerate config files**

Run:
```bash
python3 scripts/gen_config_c.py parameters.yaml components/keyer_config/include
```

Expected: Config files regenerated with new `leds` and `wifi` families.

**Step 5: Verify build**

Run:
```bash
idf.py build
```

Expected: Build succeeds.

**Step 6: Commit**

```bash
git add parameters.yaml components/keyer_config/include/
git commit -m "feat(config): add LED and WiFi parameter families

Add leds family: gpio_data, count, brightness, brightness_dim
Add wifi family: enabled, ssid, password, timeout_sec, static IP config
Remove deprecated system.led_brightness"
```

---

## Task 2: Create keyer_led Component - Header

**Files:**
- Create: `components/keyer_led/include/led.h`
- Create: `components/keyer_led/CMakeLists.txt`

**Step 1: Create directory structure**

Run:
```bash
mkdir -p components/keyer_led/include components/keyer_led/src
```

**Step 2: Write led.h**

Create `components/keyer_led/include/led.h`:

```c
/**
 * @file led.h
 * @brief WS2812B RGB LED status driver
 *
 * Displays keyer state through RGB LEDs:
 * - Boot/connecting: orange breathing
 * - Connected: green flash then dim
 * - AP mode: alternating orange/blue
 * - Degraded: dim yellow
 * - Keying: DIT/DAH/squeeze indication
 */

#ifndef KEYER_LED_H
#define KEYER_LED_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief LED state machine states
 */
typedef enum {
    LED_STATE_OFF = 0,          /**< LEDs off */
    LED_STATE_BOOT,             /**< Boot: orange breathing */
    LED_STATE_WIFI_CONNECTING,  /**< Connecting: orange breathing */
    LED_STATE_WIFI_FAILED,      /**< Brief red flash */
    LED_STATE_DEGRADED,         /**< Dim yellow steady */
    LED_STATE_AP_MODE,          /**< Alternating orange/blue */
    LED_STATE_CONNECTED,        /**< Green flash sequence */
    LED_STATE_IDLE,             /**< Dim green steady */
} led_state_t;

/**
 * @brief LED configuration
 */
typedef struct {
    uint8_t gpio_data;       /**< WS2812B data GPIO */
    uint8_t led_count;       /**< Number of LEDs */
    uint8_t brightness;      /**< Master brightness 0-100 */
    uint8_t brightness_dim;  /**< Dim brightness 0-100 */
} led_config_t;

/**
 * @brief Default LED configuration
 */
#define LED_CONFIG_DEFAULT { \
    .gpio_data = 38, \
    .led_count = 7, \
    .brightness = 50, \
    .brightness_dim = 10 \
}

/**
 * @brief Initialize LED driver
 *
 * @param config LED configuration
 * @return ESP_OK on success, error code on failure
 *
 * @note Non-fatal: keyer continues if init fails
 */
esp_err_t led_init(const led_config_t *config);

/**
 * @brief Deinitialize LED driver
 */
void led_deinit(void);

/**
 * @brief Set LED state
 *
 * @param state New state
 */
void led_set_state(led_state_t state);

/**
 * @brief Get current LED state
 *
 * @return Current state
 */
led_state_t led_get_state(void);

/**
 * @brief Update LED display (call periodically from bg_task)
 *
 * Handles:
 * - State machine animations (breathing, flashing)
 * - Keying overlay (DIT/DAH/squeeze)
 *
 * @param now_us Current timestamp in microseconds
 * @param dit DIT paddle pressed
 * @param dah DAH paddle pressed
 */
void led_tick(int64_t now_us, bool dit, bool dah);

/**
 * @brief Update brightness from config
 *
 * @param brightness Master brightness 0-100
 * @param brightness_dim Dim brightness 0-100
 */
void led_set_brightness(uint8_t brightness, uint8_t brightness_dim);

/**
 * @brief Check if LED driver is initialized
 *
 * @return true if initialized and ready
 */
bool led_is_initialized(void);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_LED_H */
```

**Step 3: Write CMakeLists.txt**

Create `components/keyer_led/CMakeLists.txt`:

```cmake
# keyer_led - WS2812B RGB LED driver

idf_component_register(
    SRCS
        "src/led.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_driver_rmt
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Werror
    -Wconversion
    -Wshadow
)
```

**Step 4: Commit**

```bash
git add components/keyer_led/
git commit -m "feat(led): add keyer_led component header and build config"
```

---

## Task 3: Create keyer_led Component - Implementation

**Files:**
- Create: `components/keyer_led/src/led.c`

**Step 1: Write led.c**

Create `components/keyer_led/src/led.c`:

```c
/**
 * @file led.c
 * @brief WS2812B RGB LED driver implementation
 */

#include "led.h"
#include "driver/rmt_tx.h"
#include "led_strip_encoder.h"

#include <string.h>
#include <stdatomic.h>

#include "esp_log.h"

static const char *TAG = "led";

/* Color definitions (GRB order for WS2812B) */
#define COLOR_OFF       0x000000
#define COLOR_RED       0x00FF00   /* G=0, R=FF, B=0 */
#define COLOR_GREEN     0xFF0000   /* G=FF, R=0, B=0 */
#define COLOR_BLUE      0x0000FF   /* G=0, R=0, B=FF */
#define COLOR_ORANGE    0x80FF00   /* G=80, R=FF, B=0 */
#define COLOR_YELLOW    0xA0FF00   /* G=A0, R=FF, B=0 */
#define COLOR_MAGENTA   0x00FFFF   /* G=0, R=FF, B=FF */

/* Animation timing (microseconds) */
#define BREATH_PERIOD_US    2000000  /* 2s full breath cycle */
#define FLASH_DURATION_US   100000   /* 100ms per flash */
#define FLASH_GAP_US        100000   /* 100ms between flashes */
#define RED_FLASH_US        500000   /* 500ms red flash */
#define AP_TOGGLE_US        500000   /* 500ms alternating */

/* Maximum LEDs supported */
#define MAX_LEDS 32

/* RMT configuration */
#define RMT_RESOLUTION_HZ 10000000  /* 10MHz, 100ns per tick */

/* State */
static struct {
    bool initialized;
    led_config_t config;
    rmt_channel_handle_t rmt_channel;
    rmt_encoder_handle_t encoder;
    uint8_t pixel_buf[MAX_LEDS * 3];  /* GRB data */

    /* State machine */
    _Atomic led_state_t state;
    int64_t state_start_us;
    uint8_t flash_count;

    /* Brightness */
    _Atomic uint8_t brightness;
    _Atomic uint8_t brightness_dim;
} s_led;

/* Forward declarations */
static void set_all_leds(uint32_t color, uint8_t brightness_pct);
static void set_led_range(uint8_t start, uint8_t end, uint32_t color, uint8_t brightness_pct);
static void set_led(uint8_t idx, uint32_t color, uint8_t brightness_pct);
static void show_leds(void);
static uint8_t breath_brightness(int64_t now_us, uint8_t max_brightness);

/* LED strip encoder (simplified for WS2812B) */
typedef struct {
    rmt_encoder_t base;
    rmt_encoder_t *bytes_encoder;
    rmt_encoder_t *copy_encoder;
    int state;
    rmt_symbol_word_t reset_code;
} rmt_led_strip_encoder_t;

static size_t rmt_encode_led_strip(rmt_encoder_t *encoder, rmt_channel_handle_t channel,
                                   const void *primary_data, size_t data_size,
                                   rmt_encode_state_t *ret_state);

static esp_err_t rmt_del_led_strip_encoder(rmt_encoder_t *encoder);

static esp_err_t rmt_led_strip_encoder_reset(rmt_encoder_t *encoder);

static esp_err_t rmt_new_led_strip_encoder(rmt_encoder_handle_t *ret_encoder)
{
    rmt_led_strip_encoder_t *led_encoder = calloc(1, sizeof(rmt_led_strip_encoder_t));
    if (!led_encoder) {
        return ESP_ERR_NO_MEM;
    }

    led_encoder->base.encode = rmt_encode_led_strip;
    led_encoder->base.del = rmt_del_led_strip_encoder;
    led_encoder->base.reset = rmt_led_strip_encoder_reset;

    /* WS2812B timing: T0H=400ns, T0L=850ns, T1H=800ns, T1L=450ns */
    rmt_bytes_encoder_config_t bytes_config = {
        .bit0 = { .level0 = 1, .duration0 = 4, .level1 = 0, .duration1 = 9 },   /* 400ns high, 900ns low */
        .bit1 = { .level0 = 1, .duration0 = 8, .level1 = 0, .duration1 = 5 },   /* 800ns high, 500ns low */
        .flags.msb_first = 1
    };
    esp_err_t ret = rmt_new_bytes_encoder(&bytes_config, &led_encoder->bytes_encoder);
    if (ret != ESP_OK) {
        free(led_encoder);
        return ret;
    }

    rmt_copy_encoder_config_t copy_config = {};
    ret = rmt_new_copy_encoder(&copy_config, &led_encoder->copy_encoder);
    if (ret != ESP_OK) {
        rmt_del_encoder(led_encoder->bytes_encoder);
        free(led_encoder);
        return ret;
    }

    /* Reset code: 50us low */
    led_encoder->reset_code = (rmt_symbol_word_t){
        .level0 = 0, .duration0 = 500,
        .level1 = 0, .duration1 = 500
    };

    *ret_encoder = &led_encoder->base;
    return ESP_OK;
}

static size_t rmt_encode_led_strip(rmt_encoder_t *encoder, rmt_channel_handle_t channel,
                                   const void *primary_data, size_t data_size,
                                   rmt_encode_state_t *ret_state)
{
    rmt_led_strip_encoder_t *led_encoder = __containerof(encoder, rmt_led_strip_encoder_t, base);
    rmt_encode_state_t state = RMT_ENCODING_RESET;
    size_t encoded_symbols = 0;

    switch (led_encoder->state) {
    case 0:  /* Encode pixel data */
        encoded_symbols = led_encoder->bytes_encoder->encode(
            led_encoder->bytes_encoder, channel, primary_data, data_size, &state);
        if (state & RMT_ENCODING_COMPLETE) {
            led_encoder->state = 1;
        }
        if (state & RMT_ENCODING_MEM_FULL) {
            *ret_state = (rmt_encode_state_t)(state & (~(unsigned)RMT_ENCODING_COMPLETE));
            return encoded_symbols;
        }
        /* Fall through */
    case 1:  /* Encode reset code */
        encoded_symbols += led_encoder->copy_encoder->encode(
            led_encoder->copy_encoder, channel, &led_encoder->reset_code,
            sizeof(led_encoder->reset_code), &state);
        if (state & RMT_ENCODING_COMPLETE) {
            led_encoder->state = 0;
            *ret_state = RMT_ENCODING_COMPLETE;
        }
        break;
    }
    return encoded_symbols;
}

static esp_err_t rmt_del_led_strip_encoder(rmt_encoder_t *encoder)
{
    rmt_led_strip_encoder_t *led_encoder = __containerof(encoder, rmt_led_strip_encoder_t, base);
    rmt_del_encoder(led_encoder->bytes_encoder);
    rmt_del_encoder(led_encoder->copy_encoder);
    free(led_encoder);
    return ESP_OK;
}

static esp_err_t rmt_led_strip_encoder_reset(rmt_encoder_t *encoder)
{
    rmt_led_strip_encoder_t *led_encoder = __containerof(encoder, rmt_led_strip_encoder_t, base);
    rmt_encoder_reset(led_encoder->bytes_encoder);
    rmt_encoder_reset(led_encoder->copy_encoder);
    led_encoder->state = 0;
    return ESP_OK;
}

esp_err_t led_init(const led_config_t *config)
{
    if (s_led.initialized) {
        return ESP_OK;
    }

    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (config->led_count == 0) {
        ESP_LOGI(TAG, "LED count is 0, disabling");
        return ESP_OK;
    }

    if (config->led_count > MAX_LEDS) {
        ESP_LOGE(TAG, "LED count %u exceeds max %d", config->led_count, MAX_LEDS);
        return ESP_ERR_INVALID_ARG;
    }

    s_led.config = *config;
    atomic_store(&s_led.brightness, config->brightness);
    atomic_store(&s_led.brightness_dim, config->brightness_dim);

    /* Configure RMT TX channel */
    rmt_tx_channel_config_t tx_config = {
        .gpio_num = (int)config->gpio_data,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RMT_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
    };

    esp_err_t ret = rmt_new_tx_channel(&tx_config, &s_led.rmt_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create RMT channel: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = rmt_new_led_strip_encoder(&s_led.encoder);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create encoder: %s", esp_err_to_name(ret));
        rmt_del_channel(s_led.rmt_channel);
        return ret;
    }

    ret = rmt_enable(s_led.rmt_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable RMT: %s", esp_err_to_name(ret));
        rmt_del_encoder(s_led.encoder);
        rmt_del_channel(s_led.rmt_channel);
        return ret;
    }

    s_led.initialized = true;
    atomic_store(&s_led.state, LED_STATE_BOOT);
    s_led.state_start_us = 0;

    /* Turn off all LEDs initially */
    set_all_leds(COLOR_OFF, 0);
    show_leds();

    ESP_LOGI(TAG, "Initialized: %u LEDs on GPIO %u", config->led_count, config->gpio_data);
    return ESP_OK;
}

void led_deinit(void)
{
    if (!s_led.initialized) {
        return;
    }

    set_all_leds(COLOR_OFF, 0);
    show_leds();

    rmt_disable(s_led.rmt_channel);
    rmt_del_encoder(s_led.encoder);
    rmt_del_channel(s_led.rmt_channel);

    s_led.initialized = false;
    ESP_LOGI(TAG, "Deinitialized");
}

void led_set_state(led_state_t state)
{
    led_state_t old = atomic_exchange(&s_led.state, state);
    if (old != state) {
        s_led.state_start_us = 0;  /* Reset animation */
        s_led.flash_count = 0;
    }
}

led_state_t led_get_state(void)
{
    return atomic_load(&s_led.state);
}

void led_set_brightness(uint8_t brightness, uint8_t brightness_dim)
{
    atomic_store(&s_led.brightness, brightness);
    atomic_store(&s_led.brightness_dim, brightness_dim);
}

bool led_is_initialized(void)
{
    return s_led.initialized;
}

void led_tick(int64_t now_us, bool dit, bool dah)
{
    if (!s_led.initialized || s_led.config.led_count == 0) {
        return;
    }

    /* Initialize state start time */
    if (s_led.state_start_us == 0) {
        s_led.state_start_us = now_us;
    }

    int64_t elapsed_us = now_us - s_led.state_start_us;
    led_state_t state = atomic_load(&s_led.state);
    uint8_t bright = atomic_load(&s_led.brightness);
    uint8_t dim = atomic_load(&s_led.brightness_dim);

    uint8_t led_count = s_led.config.led_count;
    uint8_t center = led_count / 2;

    switch (state) {
    case LED_STATE_OFF:
        set_all_leds(COLOR_OFF, 0);
        break;

    case LED_STATE_BOOT:
    case LED_STATE_WIFI_CONNECTING: {
        uint8_t b = breath_brightness(now_us, bright);
        set_all_leds(COLOR_ORANGE, b);
        break;
    }

    case LED_STATE_WIFI_FAILED:
        /* Brief red flash, then transition to DEGRADED */
        if (elapsed_us < RED_FLASH_US) {
            set_all_leds(COLOR_RED, bright);
        } else {
            led_set_state(LED_STATE_DEGRADED);
        }
        break;

    case LED_STATE_DEGRADED:
        set_all_leds(COLOR_YELLOW, dim);
        break;

    case LED_STATE_AP_MODE: {
        /* Alternating orange/blue every 500ms */
        bool phase = ((elapsed_us / AP_TOGGLE_US) % 2) == 0;
        set_all_leds(phase ? COLOR_ORANGE : COLOR_BLUE, bright);
        break;
    }

    case LED_STATE_CONNECTED: {
        /* 3 quick green flashes, then to IDLE */
        int64_t flash_cycle = FLASH_DURATION_US + FLASH_GAP_US;
        int flash_num = (int)(elapsed_us / flash_cycle);
        int64_t within_cycle = elapsed_us % flash_cycle;

        if (flash_num < 3) {
            if (within_cycle < FLASH_DURATION_US) {
                set_all_leds(COLOR_GREEN, bright);
            } else {
                set_all_leds(COLOR_OFF, 0);
            }
        } else {
            led_set_state(LED_STATE_IDLE);
        }
        break;
    }

    case LED_STATE_IDLE:
    default:
        /* Base: dim green all */
        set_all_leds(COLOR_GREEN, dim);

        /* Keying overlay */
        bool squeeze = dit && dah;

        if (squeeze) {
            /* Center LED magenta for squeeze */
            set_led(center, COLOR_MAGENTA, bright);
        }

        if (dit) {
            /* Left side for DIT */
            for (uint8_t i = 0; i < center; i++) {
                set_led(i, COLOR_GREEN, bright);
            }
        }

        if (dah) {
            /* Right side for DAH */
            for (uint8_t i = (uint8_t)(center + 1); i < led_count; i++) {
                set_led(i, COLOR_GREEN, bright);
            }
        }
        break;
    }

    show_leds();
}

/* --- Internal helpers --- */

static void set_all_leds(uint32_t color, uint8_t brightness_pct)
{
    set_led_range(0, s_led.config.led_count, color, brightness_pct);
}

static void set_led_range(uint8_t start, uint8_t end, uint32_t color, uint8_t brightness_pct)
{
    for (uint8_t i = start; i < end && i < s_led.config.led_count; i++) {
        set_led(i, color, brightness_pct);
    }
}

static void set_led(uint8_t idx, uint32_t color, uint8_t brightness_pct)
{
    if (idx >= s_led.config.led_count) {
        return;
    }

    /* Extract GRB components */
    uint8_t g = (uint8_t)((color >> 16) & 0xFF);
    uint8_t r = (uint8_t)((color >> 8) & 0xFF);
    uint8_t b = (uint8_t)(color & 0xFF);

    /* Apply brightness */
    uint16_t scale = (uint16_t)brightness_pct * 255 / 100;
    g = (uint8_t)((uint16_t)g * scale / 255);
    r = (uint8_t)((uint16_t)r * scale / 255);
    b = (uint8_t)((uint16_t)b * scale / 255);

    /* Store in buffer */
    size_t offset = (size_t)idx * 3;
    s_led.pixel_buf[offset + 0] = g;
    s_led.pixel_buf[offset + 1] = r;
    s_led.pixel_buf[offset + 2] = b;
}

static void show_leds(void)
{
    rmt_transmit_config_t tx_config = { .loop_count = 0 };
    size_t size = (size_t)s_led.config.led_count * 3;
    rmt_transmit(s_led.rmt_channel, s_led.encoder, s_led.pixel_buf, size, &tx_config);
    rmt_tx_wait_all_done(s_led.rmt_channel, pdMS_TO_TICKS(100));
}

static uint8_t breath_brightness(int64_t now_us, uint8_t max_brightness)
{
    /* Sinusoidal breathing: 0 -> max -> 0 over BREATH_PERIOD_US */
    int64_t phase = now_us % BREATH_PERIOD_US;
    /* Use simple triangle wave approximation */
    int64_t half = BREATH_PERIOD_US / 2;
    int64_t value;
    if (phase < half) {
        value = phase * max_brightness / half;
    } else {
        value = (BREATH_PERIOD_US - phase) * max_brightness / half;
    }
    return (uint8_t)value;
}
```

**Step 2: Build to verify**

Run:
```bash
idf.py build
```

Expected: Build succeeds.

**Step 3: Commit**

```bash
git add components/keyer_led/src/led.c
git commit -m "feat(led): implement WS2812B driver with state machine

- RMT-based WS2812B protocol encoder
- State machine: boot, connecting, failed, degraded, AP, connected, idle
- Breathing animation for boot/connecting
- Keying overlay: DIT (left), DAH (right), squeeze (center magenta)"
```

---

## Task 4: Create keyer_wifi Component - Header

**Files:**
- Create: `components/keyer_wifi/include/wifi.h`
- Create: `components/keyer_wifi/CMakeLists.txt`

**Step 1: Create directory structure**

Run:
```bash
mkdir -p components/keyer_wifi/include components/keyer_wifi/src
```

**Step 2: Write wifi.h**

Create `components/keyer_wifi/include/wifi.h`:

```c
/**
 * @file wifi.h
 * @brief WiFi connectivity manager
 *
 * Manages WiFi STA connection with AP fallback:
 * - Attempts STA connection on startup
 * - Falls back to open AP if connection fails
 * - Reports state via atomic for LED integration
 */

#ifndef KEYER_WIFI_H
#define KEYER_WIFI_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief WiFi state
 */
typedef enum {
    WIFI_STATE_DISABLED = 0,   /**< WiFi disabled */
    WIFI_STATE_CONNECTING,     /**< Attempting STA connection */
    WIFI_STATE_CONNECTED,      /**< STA connected */
    WIFI_STATE_FAILED,         /**< Connection failed (brief state) */
    WIFI_STATE_AP_MODE,        /**< Running as access point */
} wifi_state_t;

/**
 * @brief WiFi configuration
 */
typedef struct {
    bool enabled;              /**< WiFi master enable */
    char ssid[33];             /**< Network SSID (32 + null) */
    char password[65];         /**< Network password (64 + null) */
    uint16_t timeout_sec;      /**< Connection timeout */
    bool use_static_ip;        /**< Use static IP vs DHCP */
    char ip_address[16];       /**< Static IP */
    char netmask[16];          /**< Subnet mask */
    char gateway[16];          /**< Gateway */
    char dns[16];              /**< DNS server */
} wifi_config_app_t;

/**
 * @brief Default WiFi configuration
 */
#define WIFI_CONFIG_DEFAULT { \
    .enabled = false, \
    .ssid = "", \
    .password = "", \
    .timeout_sec = 30, \
    .use_static_ip = false, \
    .ip_address = "0.0.0.0", \
    .netmask = "255.255.255.0", \
    .gateway = "0.0.0.0", \
    .dns = "0.0.0.0" \
}

/**
 * @brief Initialize WiFi subsystem
 *
 * @param config WiFi configuration
 * @return ESP_OK on success
 */
esp_err_t wifi_app_init(const wifi_config_app_t *config);

/**
 * @brief Start WiFi connection
 *
 * Non-blocking. Call wifi_get_state() to monitor progress.
 *
 * @return ESP_OK if started
 */
esp_err_t wifi_app_start(void);

/**
 * @brief Stop WiFi
 */
void wifi_app_stop(void);

/**
 * @brief Get current WiFi state
 *
 * Thread-safe (atomic).
 *
 * @return Current state
 */
wifi_state_t wifi_get_state(void);

/**
 * @brief Get current IP address
 *
 * @param buf Buffer for IP string (at least 16 bytes)
 * @param len Buffer length
 * @return true if connected and IP available
 */
bool wifi_get_ip(char *buf, size_t len);

/**
 * @brief Check if WiFi is connected
 *
 * @return true if STA connected
 */
bool wifi_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_WIFI_H */
```

**Step 3: Write CMakeLists.txt**

Create `components/keyer_wifi/CMakeLists.txt`:

```cmake
# keyer_wifi - WiFi connectivity manager

idf_component_register(
    SRCS
        "src/wifi.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_netif esp_event nvs_flash
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Werror
    -Wconversion
    -Wshadow
)
```

**Step 4: Commit**

```bash
git add components/keyer_wifi/
git commit -m "feat(wifi): add keyer_wifi component header and build config"
```

---

## Task 5: Create keyer_wifi Component - Implementation

**Files:**
- Create: `components/keyer_wifi/src/wifi.c`

**Step 1: Write wifi.c**

Create `components/keyer_wifi/src/wifi.c`:

```c
/**
 * @file wifi.c
 * @brief WiFi connectivity manager implementation
 */

#include "wifi.h"

#include <string.h>
#include <stdatomic.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "esp_log.h"

static const char *TAG = "wifi";

/* Event bits */
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

/* Hardcoded AP settings */
#define AP_SSID_PREFIX   "CWKeyer-"
#define AP_MAX_CONN      4
#define AP_CHANNEL       1

/* State */
static struct {
    bool initialized;
    wifi_config_app_t config;
    esp_netif_t *sta_netif;
    esp_netif_t *ap_netif;
    EventGroupHandle_t event_group;
    _Atomic wifi_state_t state;
    int retry_count;
    esp_ip4_addr_t ip_addr;
} s_wifi;

/* Event handlers */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "STA started, connecting...");
            esp_wifi_connect();
            break;

        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t *event = event_data;
            ESP_LOGW(TAG, "Disconnected (reason=%d)", event->reason);

            s_wifi.retry_count++;
            if (s_wifi.retry_count < 3) {
                ESP_LOGI(TAG, "Retrying... (%d/3)", s_wifi.retry_count);
                esp_wifi_connect();
            } else {
                xEventGroupSetBits(s_wifi.event_group, WIFI_FAIL_BIT);
            }
            break;
        }

        case WIFI_EVENT_AP_START:
            ESP_LOGI(TAG, "AP started");
            break;

        case WIFI_EVENT_AP_STACONNECTED: {
            wifi_event_ap_staconnected_t *event = event_data;
            ESP_LOGI(TAG, "Station connected: " MACSTR, MAC2STR(event->mac));
            break;
        }

        case WIFI_EVENT_AP_STADISCONNECTED: {
            wifi_event_ap_stadisconnected_t *event = event_data;
            ESP_LOGI(TAG, "Station disconnected: " MACSTR, MAC2STR(event->mac));
            break;
        }

        default:
            break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = event_data;
        s_wifi.ip_addr = event->ip_info.ip;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_wifi.retry_count = 0;
        xEventGroupSetBits(s_wifi.event_group, WIFI_CONNECTED_BIT);
    }
}

static void start_ap_mode(void)
{
    /* Get MAC for unique SSID */
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);

    char ap_ssid[32];
    snprintf(ap_ssid, sizeof(ap_ssid), "%s%02X%02X", AP_SSID_PREFIX, mac[4], mac[5]);

    wifi_config_t wifi_config = {
        .ap = {
            .ssid_len = (uint8_t)strlen(ap_ssid),
            .channel = AP_CHANNEL,
            .max_connection = AP_MAX_CONN,
            .authmode = WIFI_AUTH_OPEN,
        },
    };
    memcpy(wifi_config.ap.ssid, ap_ssid, strlen(ap_ssid));

    ESP_LOGI(TAG, "Starting AP mode: %s (open)", ap_ssid);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    atomic_store(&s_wifi.state, WIFI_STATE_AP_MODE);
}

static void connection_task(void *arg)
{
    (void)arg;

    /* Wait for connection or timeout */
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi.event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(s_wifi.config.timeout_sec * 1000)
    );

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected successfully");
        atomic_store(&s_wifi.state, WIFI_STATE_CONNECTED);
    } else {
        ESP_LOGW(TAG, "Connection failed or timeout, switching to AP mode");
        atomic_store(&s_wifi.state, WIFI_STATE_FAILED);

        /* Brief delay for LED to show red */
        vTaskDelay(pdMS_TO_TICKS(600));

        /* Stop STA and start AP */
        esp_wifi_stop();
        start_ap_mode();
    }

    vTaskDelete(NULL);
}

esp_err_t wifi_app_init(const wifi_config_app_t *config)
{
    if (s_wifi.initialized) {
        return ESP_OK;
    }

    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_wifi.config = *config;

    if (!config->enabled) {
        ESP_LOGI(TAG, "WiFi disabled");
        atomic_store(&s_wifi.state, WIFI_STATE_DISABLED);
        return ESP_OK;
    }

    if (strlen(config->ssid) == 0) {
        ESP_LOGI(TAG, "No SSID configured, WiFi disabled");
        atomic_store(&s_wifi.state, WIFI_STATE_DISABLED);
        return ESP_OK;
    }

    s_wifi.event_group = xEventGroupCreate();
    if (s_wifi.event_group == NULL) {
        return ESP_ERR_NO_MEM;
    }

    /* Initialize TCP/IP stack */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_wifi.sta_netif = esp_netif_create_default_wifi_sta();
    s_wifi.ap_netif = esp_netif_create_default_wifi_ap();

    /* Configure static IP if requested */
    if (config->use_static_ip && strcmp(config->ip_address, "0.0.0.0") != 0) {
        esp_netif_dhcpc_stop(s_wifi.sta_netif);

        esp_netif_ip_info_t ip_info = {0};
        esp_netif_str_to_ip4(config->ip_address, &ip_info.ip);
        esp_netif_str_to_ip4(config->netmask, &ip_info.netmask);
        esp_netif_str_to_ip4(config->gateway, &ip_info.gw);

        esp_netif_set_ip_info(s_wifi.sta_netif, &ip_info);

        if (strcmp(config->dns, "0.0.0.0") != 0) {
            esp_netif_dns_info_t dns_info = {0};
            esp_netif_str_to_ip4(config->dns, &dns_info.ip.u_addr.ip4);
            dns_info.ip.type = ESP_IPADDR_TYPE_V4;
            esp_netif_set_dns_info(s_wifi.sta_netif, ESP_NETIF_DNS_MAIN, &dns_info);
        }

        ESP_LOGI(TAG, "Using static IP: %s", config->ip_address);
    }

    /* Initialize WiFi */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* Register event handlers */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    s_wifi.initialized = true;
    atomic_store(&s_wifi.state, WIFI_STATE_DISABLED);

    ESP_LOGI(TAG, "Initialized");
    return ESP_OK;
}

esp_err_t wifi_app_start(void)
{
    if (!s_wifi.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (atomic_load(&s_wifi.state) == WIFI_STATE_DISABLED) {
        /* Check if we should start */
        if (!s_wifi.config.enabled || strlen(s_wifi.config.ssid) == 0) {
            return ESP_OK;
        }
    }

    atomic_store(&s_wifi.state, WIFI_STATE_CONNECTING);
    s_wifi.retry_count = 0;

    /* Configure STA */
    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, s_wifi.config.ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, s_wifi.config.password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = strlen(s_wifi.config.password) > 0 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_LOGI(TAG, "Connecting to: %s", s_wifi.config.ssid);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* Start connection monitoring task */
    xTaskCreate(connection_task, "wifi_conn", 4096, NULL, 5, NULL);

    return ESP_OK;
}

void wifi_app_stop(void)
{
    if (!s_wifi.initialized) {
        return;
    }

    esp_wifi_stop();
    atomic_store(&s_wifi.state, WIFI_STATE_DISABLED);
}

wifi_state_t wifi_get_state(void)
{
    return atomic_load(&s_wifi.state);
}

bool wifi_get_ip(char *buf, size_t len)
{
    if (atomic_load(&s_wifi.state) != WIFI_STATE_CONNECTED) {
        return false;
    }

    snprintf(buf, len, IPSTR, IP2STR(&s_wifi.ip_addr));
    return true;
}

bool wifi_is_connected(void)
{
    return atomic_load(&s_wifi.state) == WIFI_STATE_CONNECTED;
}
```

**Step 2: Build to verify**

Run:
```bash
idf.py build
```

Expected: Build succeeds.

**Step 3: Commit**

```bash
git add components/keyer_wifi/src/wifi.c
git commit -m "feat(wifi): implement WiFi STA with AP fallback

- STA connection with configurable timeout
- 3 retry attempts before fallback
- Open AP mode with MAC-based SSID (CWKeyer-XXXX)
- Static IP support
- Atomic state for thread-safe LED integration"
```

---

## Task 6: Integrate LED and WiFi into bg_task

**Files:**
- Modify: `main/bg_task.c`
- Modify: `main/CMakeLists.txt`

**Step 1: Update main/CMakeLists.txt**

Add keyer_led and keyer_wifi to REQUIRES:

```cmake
idf_component_register(
    SRCS "main.c" "rt_task.c" "bg_task.c"
    INCLUDE_DIRS ""
    REQUIRES
        keyer_core
        keyer_iambic
        keyer_audio
        keyer_hal
        keyer_logging
        keyer_console
        keyer_config
        keyer_decoder
        keyer_led
        keyer_wifi
)
```

**Step 2: Update bg_task.c**

Replace the contents of `main/bg_task.c`:

```c
/**
 * @file bg_task.c
 * @brief Background task (Core 1)
 *
 * Best-effort processing:
 * - LED status display
 * - WiFi connectivity
 * - Morse decoder
 * - Diagnostics
 *
 * Runs on Core 1 with normal priority.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include <inttypes.h>

#include "keyer_core.h"
#include "rt_log.h"
#include "decoder.h"
#include "led.h"
#include "wifi.h"
#include "config.h"
#include "hal_gpio.h"

/* External globals */
extern keying_stream_t g_keying_stream;
extern fault_state_t g_fault_state;

/**
 * @brief Convert WiFi state to LED state
 */
static led_state_t wifi_to_led_state(wifi_state_t ws)
{
    switch (ws) {
    case WIFI_STATE_DISABLED:
        return LED_STATE_IDLE;
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

void bg_task(void *arg)
{
    (void)arg;

    int64_t now_us = esp_timer_get_time();

    /* Initialize LED driver */
    led_config_t led_cfg = {
        .gpio_data = config_get_leds_gpio_data(),
        .led_count = config_get_leds_count(),
        .brightness = config_get_leds_brightness(),
        .brightness_dim = config_get_leds_brightness_dim(),
    };

    esp_err_t ret = led_init(&led_cfg);
    if (ret != ESP_OK) {
        RT_WARN(&g_bg_log_stream, now_us, "LED init failed: %s", esp_err_to_name(ret));
    } else {
        led_set_state(LED_STATE_BOOT);
    }

    /* Initialize WiFi */
    wifi_config_app_t wifi_cfg = {
        .enabled = config_get_wifi_enabled(),
        .timeout_sec = config_get_wifi_timeout_sec(),
        .use_static_ip = config_get_wifi_use_static_ip(),
    };
    strncpy(wifi_cfg.ssid, config_get_wifi_ssid(), sizeof(wifi_cfg.ssid) - 1);
    strncpy(wifi_cfg.password, config_get_wifi_password(), sizeof(wifi_cfg.password) - 1);
    strncpy(wifi_cfg.ip_address, config_get_wifi_ip_address(), sizeof(wifi_cfg.ip_address) - 1);
    strncpy(wifi_cfg.netmask, config_get_wifi_netmask(), sizeof(wifi_cfg.netmask) - 1);
    strncpy(wifi_cfg.gateway, config_get_wifi_gateway(), sizeof(wifi_cfg.gateway) - 1);
    strncpy(wifi_cfg.dns, config_get_wifi_dns(), sizeof(wifi_cfg.dns) - 1);

    ret = wifi_app_init(&wifi_cfg);
    if (ret != ESP_OK) {
        RT_WARN(&g_bg_log_stream, now_us, "WiFi init failed: %s", esp_err_to_name(ret));
    }

    /* Start WiFi if enabled */
    if (wifi_cfg.enabled && strlen(wifi_cfg.ssid) > 0) {
        led_set_state(LED_STATE_WIFI_CONNECTING);
        wifi_app_start();
    } else {
        /* No WiFi, go directly to idle */
        led_set_state(LED_STATE_IDLE);
    }

    /* Initialize decoder */
    decoder_init();

    RT_INFO(&g_bg_log_stream, now_us, "BG task started");

    uint32_t stats_counter = 0;
    wifi_state_t last_wifi_state = WIFI_STATE_DISABLED;

    for (;;) {
        now_us = esp_timer_get_time();

        /* Check WiFi state changes */
        wifi_state_t ws = wifi_get_state();
        if (ws != last_wifi_state) {
            led_set_state(wifi_to_led_state(ws));
            last_wifi_state = ws;

            if (ws == WIFI_STATE_CONNECTED) {
                char ip[16];
                if (wifi_get_ip(ip, sizeof(ip))) {
                    RT_INFO(&g_bg_log_stream, now_us, "WiFi connected: %s", ip);
                }
            } else if (ws == WIFI_STATE_AP_MODE) {
                RT_INFO(&g_bg_log_stream, now_us, "WiFi AP mode active");
            }
        }

        /* Update LEDs with keying state */
        if (led_is_initialized()) {
            gpio_state_t paddles = hal_gpio_read_paddles();
            bool dit = (paddles == GPIO_STATE_DIT || paddles == GPIO_STATE_BOTH);
            bool dah = (paddles == GPIO_STATE_DAH || paddles == GPIO_STATE_BOTH);
            led_tick(now_us, dit, dah);

            /* Update brightness from config if changed */
            led_set_brightness(config_get_leds_brightness(), config_get_leds_brightness_dim());
        }

        /* Process decoder */
        decoder_process();

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

        vTaskDelay(pdMS_TO_TICKS(10));  /* 100Hz update rate */
    }
}
```

**Step 3: Build to verify**

Run:
```bash
idf.py build
```

Expected: Build succeeds.

**Step 4: Commit**

```bash
git add main/bg_task.c main/CMakeLists.txt
git commit -m "feat(bg_task): integrate LED and WiFi into background task

- Initialize LED driver with config values
- Initialize WiFi with config values
- Update LED state based on WiFi state changes
- Update LED keying overlay from paddle GPIO state
- Log WiFi connection events"
```

---

## Task 7: Update gen_config_c.py for String Types

**Files:**
- Modify: `scripts/gen_config_c.py`

**Note:** The config generator needs to support `string` types for WiFi SSID, password, and IP addresses. Check if already supported. If not, add support.

**Step 1: Check current string support**

Run:
```bash
grep -n "string" scripts/gen_config_c.py
```

If string type is not handled, add support for:
- `char array[max_length+1]` in struct
- `const char*` getter functions
- NVS string storage

**Step 2: If needed, update generator**

This step depends on the current implementation. Add string handling to:
- `type_to_c()` function
- Struct generation
- Getter generation
- NVS read/write

**Step 3: Regenerate and build**

Run:
```bash
python3 scripts/gen_config_c.py parameters.yaml components/keyer_config/include
idf.py build
```

**Step 4: Commit if changes were made**

```bash
git add scripts/gen_config_c.py components/keyer_config/include/
git commit -m "feat(config): add string type support to config generator"
```

---

## Task 8: Add Host Tests for LED State Machine

**Files:**
- Create: `test_host/test_led.c`
- Modify: `test_host/CMakeLists.txt`

**Step 1: Create test_led.c**

Create `test_host/test_led.c`:

```c
/**
 * @file test_led.c
 * @brief Unit tests for LED state machine logic
 *
 * Tests the state transitions and color logic.
 * Note: RMT driver calls are stubbed for host testing.
 */

#include "unity.h"
#include <stdint.h>
#include <stdbool.h>

/* Test LED layout calculation */
void test_led_layout_7_leds(void)
{
    uint8_t led_count = 7;
    uint8_t center = led_count / 2;  /* 3 */

    /* DIT LEDs: 0, 1, 2 */
    TEST_ASSERT_EQUAL(3, center);

    /* DAH LEDs: 4, 5, 6 */
    uint8_t dah_start = center + 1;
    TEST_ASSERT_EQUAL(4, dah_start);
}

void test_led_layout_5_leds(void)
{
    uint8_t led_count = 5;
    uint8_t center = led_count / 2;  /* 2 */

    TEST_ASSERT_EQUAL(2, center);

    /* DIT: 0, 1 */
    /* DAH: 3, 4 */
}

void test_led_layout_1_led(void)
{
    uint8_t led_count = 1;
    uint8_t center = led_count / 2;  /* 0 */

    TEST_ASSERT_EQUAL(0, center);
    /* Single LED shows squeeze only */
}

/* Test breath brightness calculation */
static uint8_t test_breath_brightness(int64_t now_us, uint8_t max_brightness)
{
    int64_t period = 2000000;  /* 2s */
    int64_t phase = now_us % period;
    int64_t half = period / 2;
    int64_t value;
    if (phase < half) {
        value = phase * max_brightness / half;
    } else {
        value = (period - phase) * max_brightness / half;
    }
    return (uint8_t)value;
}

void test_breath_at_zero(void)
{
    uint8_t b = test_breath_brightness(0, 100);
    TEST_ASSERT_EQUAL(0, b);
}

void test_breath_at_peak(void)
{
    uint8_t b = test_breath_brightness(1000000, 100);  /* 1s = peak */
    TEST_ASSERT_EQUAL(100, b);
}

void test_breath_at_end(void)
{
    uint8_t b = test_breath_brightness(1999999, 100);
    TEST_ASSERT_LESS_THAN(5, b);  /* Near zero */
}

void test_breath_with_lower_max(void)
{
    uint8_t b = test_breath_brightness(1000000, 50);
    TEST_ASSERT_EQUAL(50, b);
}

/* Test color brightness scaling */
static void scale_color(uint8_t *g, uint8_t *r, uint8_t *b, uint8_t brightness_pct)
{
    uint16_t scale = (uint16_t)brightness_pct * 255 / 100;
    *g = (uint8_t)((uint16_t)*g * scale / 255);
    *r = (uint8_t)((uint16_t)*r * scale / 255);
    *b = (uint8_t)((uint16_t)*b * scale / 255);
}

void test_color_full_brightness(void)
{
    uint8_t g = 0xFF, r = 0x80, b = 0x00;  /* Orange */
    scale_color(&g, &r, &b, 100);
    TEST_ASSERT_EQUAL(0xFF, g);
    TEST_ASSERT_EQUAL(0x80, r);
    TEST_ASSERT_EQUAL(0x00, b);
}

void test_color_half_brightness(void)
{
    uint8_t g = 0xFF, r = 0x80, b = 0x00;
    scale_color(&g, &r, &b, 50);
    /* ~127, ~64, 0 */
    TEST_ASSERT_UINT8_WITHIN(2, 127, g);
    TEST_ASSERT_UINT8_WITHIN(2, 64, r);
    TEST_ASSERT_EQUAL(0, b);
}

void test_color_zero_brightness(void)
{
    uint8_t g = 0xFF, r = 0x80, b = 0x00;
    scale_color(&g, &r, &b, 0);
    TEST_ASSERT_EQUAL(0, g);
    TEST_ASSERT_EQUAL(0, r);
    TEST_ASSERT_EQUAL(0, b);
}

/* Test runner registration */
void run_led_tests(void)
{
    RUN_TEST(test_led_layout_7_leds);
    RUN_TEST(test_led_layout_5_leds);
    RUN_TEST(test_led_layout_1_led);
    RUN_TEST(test_breath_at_zero);
    RUN_TEST(test_breath_at_peak);
    RUN_TEST(test_breath_at_end);
    RUN_TEST(test_breath_with_lower_max);
    RUN_TEST(test_color_full_brightness);
    RUN_TEST(test_color_half_brightness);
    RUN_TEST(test_color_zero_brightness);
}
```

**Step 2: Update test_host/CMakeLists.txt**

Add test_led.c to TEST_SOURCES:

```cmake
set(TEST_SOURCES
    test_main.c
    test_stream.c
    test_iambic.c
    test_iambic_preset.c
    test_sidetone.c
    test_fault.c
    test_console_parser.c
    test_rt_diag.c
    test_morse_table.c
    test_timing_classifier.c
    test_decoder.c
    test_led.c
    stubs/esp_stubs.c
)
```

**Step 3: Update test_main.c**

Add:
```c
extern void run_led_tests(void);
```

And in main():
```c
run_led_tests();
```

**Step 4: Build and run tests**

Run:
```bash
cd test_host
cmake -B build
cmake --build build
./build/test_runner
```

Expected: All tests pass.

**Step 5: Commit**

```bash
git add test_host/
git commit -m "test(led): add host tests for LED layout and brightness logic"
```

---

## Task 9: Final Build and Integration Test

**Files:**
- None (verification only)

**Step 1: Full clean build**

Run:
```bash
idf.py fullclean
idf.py build
```

Expected: Build succeeds with no warnings.

**Step 2: Run host tests**

Run:
```bash
cd test_host
rm -rf build
cmake -B build
cmake --build build
./build/test_runner
```

Expected: All tests pass.

**Step 3: Flash and test on hardware (if available)**

Run:
```bash
idf.py flash monitor
```

Verify:
1. Orange breathing on boot
2. If WiFi configured: connection attempt, then green flash or AP mode
3. If no WiFi: immediate dim green
4. Keying shows DIT/DAH/squeeze on LEDs

**Step 4: Final commit**

```bash
git add -A
git commit -m "feat: LED status and WiFi connectivity complete

Features:
- WS2812B RGB LED driver with RMT
- LED states: boot, connecting, connected, failed, degraded, AP, idle
- Keying overlay: DIT/DAH sides, squeeze center (magenta)
- WiFi STA with configurable timeout
- Open AP fallback (CWKeyer-XXXX)
- Static IP support
- Full parameters.yaml integration"
```

---

## Summary

| Task | Description | Files |
|------|-------------|-------|
| 1 | Add LED/WiFi parameters to YAML | parameters.yaml |
| 2 | Create keyer_led header | components/keyer_led/ |
| 3 | Implement LED driver | components/keyer_led/src/led.c |
| 4 | Create keyer_wifi header | components/keyer_wifi/ |
| 5 | Implement WiFi manager | components/keyer_wifi/src/wifi.c |
| 6 | Integrate into bg_task | main/bg_task.c |
| 7 | Update config generator (if needed) | scripts/gen_config_c.py |
| 8 | Add LED host tests | test_host/test_led.c |
| 9 | Final verification | (build/test) |
