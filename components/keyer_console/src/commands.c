/**
 * @file commands.c
 * @brief Command registry and handlers
 *
 * Implements all console commands: help, version, stats, reboot, etc.
 */

#include "console.h"
#include "config.h"
#include "config_console.h"
#include "config_nvs.h"
#include "rt_log.h"
#include "hal_gpio.h"
#include "decoder.h"
#include "text_keyer.h"
#include "text_memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "driver/gpio.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "usb_console.h"
#include "usb_log.h"
#include "usb_uf2.h"
#include "usb_cdc.h"
#include "tusb_cdc_acm.h"
#include "wifi.h"
/* Use USB console printf for command output (skip for IDE analyzers) */
#if !defined(__INTELLISENSE__) && !defined(__clang_analyzer__) && !defined(__clangd__)
#define printf usb_console_printf
#endif
#endif

/* ============================================================================
 * Error code helpers
 * ============================================================================ */

const char *console_error_code(console_error_t err) {
    switch (err) {
        case CONSOLE_OK:                  return "OK";
        case CONSOLE_ERR_UNKNOWN_CMD:     return "E01";
        case CONSOLE_ERR_INVALID_VALUE:   return "E02";
        case CONSOLE_ERR_MISSING_ARG:     return "E03";
        case CONSOLE_ERR_OUT_OF_RANGE:    return "E04";
        case CONSOLE_ERR_REQUIRES_CONFIRM: return "E05";
        case CONSOLE_ERR_NVS_ERROR:       return "E06";
        default:                          return "E??";
    }
}

const char *console_error_message(console_error_t err) {
    switch (err) {
        case CONSOLE_OK:                  return "success";
        case CONSOLE_ERR_UNKNOWN_CMD:     return "unknown command";
        case CONSOLE_ERR_INVALID_VALUE:   return "invalid value";
        case CONSOLE_ERR_MISSING_ARG:     return "missing argument";
        case CONSOLE_ERR_OUT_OF_RANGE:    return "out of range";
        case CONSOLE_ERR_REQUIRES_CONFIRM: return "requires 'confirm'";
        case CONSOLE_ERR_NVS_ERROR:       return "NVS error";
        default:                          return "unknown error";
    }
}

/* ============================================================================
 * Command handlers
 * ============================================================================ */

/**
 * @brief Show detailed help for a command
 */
static void show_command_help(const console_cmd_t *c) {
    printf("%s - %s\r\n", c->name, c->brief);
    if (c->usage != NULL) {
        printf("\r\nUsage:\r\n%s\r\n", c->usage);
    }
}

/**
 * @brief help [cmd|family] - List commands or show help for command/family
 */
static console_error_t cmd_help(const console_parsed_cmd_t *cmd) {
    if (cmd->argc > 0 && cmd->args[0] != NULL) {
        /* Check if asking about a family */
        const family_descriptor_t *f = config_find_family(cmd->args[0]);
        if (f != NULL) {
            printf("Family: %s\r\n", f->name);
            printf("Aliases: %s\r\n", f->aliases);
            printf("%s\r\n\r\n", f->description);
            printf("Parameters:\r\n");
            for (size_t i = 0; i < CONSOLE_PARAM_COUNT; i++) {
                if (strcmp(CONSOLE_PARAMS[i].family, f->name) == 0) {
                    char buf[32];
                    config_get_param_str(CONSOLE_PARAMS[i].full_path, buf, sizeof(buf));
                    printf("  %s = %s\r\n", CONSOLE_PARAMS[i].full_path, buf);
                }
            }
            return CONSOLE_OK;
        }

        /* Help for specific command */
        const console_cmd_t *c = console_find_command(cmd->args[0]);
        if (c == NULL) {
            return CONSOLE_ERR_UNKNOWN_CMD;
        }
        show_command_help(c);
    } else {
        /* List all commands */
        size_t count;
        const console_cmd_t *cmds = console_get_commands(&count);
        printf("Commands:\r\n");
        for (size_t i = 0; i < count; i++) {
            printf("  %-14s %s\r\n", cmds[i].name, cmds[i].brief);
        }
        printf("\r\nFamilies (use 'help <family>'):\r\n");
        for (size_t i = 0; i < FAMILY_COUNT; i++) {
            printf("  %-14s (%s) %s\r\n",
                   CONSOLE_FAMILIES[i].name,
                   CONSOLE_FAMILIES[i].aliases,
                   CONSOLE_FAMILIES[i].description);
        }
        printf("\r\nType 'help <cmd>' or 'help <family>' for details\r\n");
    }
    return CONSOLE_OK;
}

/**
 * @brief ? - Alias for help
 */
static console_error_t cmd_question(const console_parsed_cmd_t *cmd) {
    return cmd_help(cmd);
}

/**
 * @brief version (v) - Show version info
 */
static console_error_t cmd_version(const console_parsed_cmd_t *cmd) {
    (void)cmd;
    printf("CW Keyer v0.1.0\r\n");
#ifdef ESP_PLATFORM
    printf("ESP-IDF: %s\r\n", esp_get_idf_version());
    printf("Target: %s\r\n", CONFIG_IDF_TARGET);
#else
    printf("Host build\r\n");
#endif
    return CONSOLE_OK;
}

/**
 * @brief stats [tasks|heap|stream|rt] - System statistics
 */
static console_error_t cmd_stats(const console_parsed_cmd_t *cmd) {
#ifdef ESP_PLATFORM
    if (cmd->argc == 0) {
        /* Overview */
        int64_t uptime_us = esp_timer_get_time();
        int64_t uptime_s = uptime_us / 1000000;
        int64_t hours = uptime_s / 3600;
        int64_t mins = (uptime_s % 3600) / 60;
        int64_t secs = uptime_s % 60;

        uint32_t heap_free = esp_get_free_heap_size();
        uint32_t heap_min = esp_get_minimum_free_heap_size();

        printf("uptime: %lld:%02lld:%02lld\r\n", hours, mins, secs);
        printf("heap: %lu bytes free (min: %lu)\r\n",
               (unsigned long)heap_free, (unsigned long)heap_min);
        printf("stream: ok\r\n");

        /* WiFi status */
        wifi_state_t wifi_state = wifi_get_state();
        const char *state_str;
        switch (wifi_state) {
            case WIFI_STATE_DISABLED:   state_str = "disabled"; break;
            case WIFI_STATE_CONNECTING: state_str = "connecting"; break;
            case WIFI_STATE_CONNECTED:  state_str = "connected"; break;
            case WIFI_STATE_FAILED:     state_str = "failed"; break;
            case WIFI_STATE_AP_MODE:    state_str = "AP mode"; break;
            default:                    state_str = "unknown"; break;
        }
        printf("wifi: %s\r\n", state_str);

        if (wifi_state == WIFI_STATE_CONNECTED || wifi_state == WIFI_STATE_AP_MODE) {
            printf("ssid: %s\r\n", g_config.wifi.ssid);
            char ip_buf[16];
            if (wifi_get_ip(ip_buf, sizeof(ip_buf))) {
                printf("ip: %s\r\n", ip_buf);
            }
        }
    } else if (strcmp(cmd->args[0], "heap") == 0) {
        uint32_t heap_free = esp_get_free_heap_size();
        uint32_t heap_min = esp_get_minimum_free_heap_size();
        printf("heap free:    %lu bytes\r\n", (unsigned long)heap_free);
        printf("heap minimum: %lu bytes\r\n", (unsigned long)heap_min);
    } else if (strcmp(cmd->args[0], "tasks") == 0) {
        /* Get number of tasks */
        UBaseType_t num_tasks = uxTaskGetNumberOfTasks();
        TaskStatus_t *tasks = malloc(num_tasks * sizeof(TaskStatus_t));
        if (tasks == NULL) {
            printf("Out of memory\r\n");
            return CONSOLE_OK;
        }

        uint32_t total_runtime;
        UBaseType_t actual = uxTaskGetSystemState(tasks, num_tasks, &total_runtime);

        printf("=== Tasks (%u) ===\r\n", (unsigned)actual);
        printf("%-16s %4s %6s %4s %4s\r\n", "NAME", "CORE", "STACK", "PRIO", "STATE");

        for (UBaseType_t i = 0; i < actual; i++) {
            const char *state;
            switch (tasks[i].eCurrentState) {
                case eRunning:   state = "RUN"; break;
                case eReady:     state = "RDY"; break;
                case eBlocked:   state = "BLK"; break;
                case eSuspended: state = "SUS"; break;
                case eDeleted:   state = "DEL"; break;
                default:         state = "???"; break;
            }

            int core = xTaskGetCoreID(tasks[i].xHandle);
            const char *core_str = (core == 0) ? "0" : (core == 1) ? "1" : "*";

            printf("%-16s %4s %6u %4u %4s\r\n",
                   tasks[i].pcTaskName,
                   core_str,
                   (unsigned)tasks[i].usStackHighWaterMark,
                   (unsigned)tasks[i].uxCurrentPriority,
                   state);
        }

        free(tasks);
    } else if (strcmp(cmd->args[0], "stream") == 0) {
        printf("stream: ok\r\n");
    } else if (strcmp(cmd->args[0], "rt") == 0) {
        printf("rt: ok\r\n");
    } else {
        return CONSOLE_ERR_INVALID_VALUE;
    }
#else
    (void)cmd;
    printf("stats not available on host\r\n");
#endif
    return CONSOLE_OK;
}

/**
 * @brief reboot confirm - Restart system
 */
static console_error_t cmd_reboot(const console_parsed_cmd_t *cmd) {
    if (cmd->argc == 0 || strcmp(cmd->args[0], "confirm") != 0) {
        return CONSOLE_ERR_REQUIRES_CONFIRM;
    }
#ifdef ESP_PLATFORM
    printf("Rebooting...\r\n");
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
#else
    printf("reboot not available on host\r\n");
#endif
    return CONSOLE_OK;
}

/**
 * @brief save - Persist configuration to NVS
 */
static console_error_t cmd_save(const console_parsed_cmd_t *cmd) {
    (void)cmd;
#ifdef ESP_PLATFORM
    int ret = config_save_to_nvs();
    if (ret < 0) {
        return CONSOLE_ERR_NVS_ERROR;
    }
    printf("Saved %d parameters to NVS\r\n", ret);
#else
    printf("NVS not available on host\r\n");
#endif
    return CONSOLE_OK;
}

/* Visitor callback for show command */
static void show_param_visitor(const param_descriptor_t *p, void *ctx) {
    (void)ctx;
    char buf[32];
    config_get_param_str(p->full_path, buf, sizeof(buf));
    printf("%s=%s\r\n", p->full_path, buf);
}

/**
 * @brief show [pattern] - Show parameters matching pattern
 *
 * Patterns:
 *   show                  All parameters
 *   show keyer.wpm        Single parameter
 *   show keyer.*          All keyer parameters
 *   show keyer.**         Keyer + subfamilies
 *   show hw.*             Alias for hardware.*
 */
static console_error_t cmd_show(const console_parsed_cmd_t *cmd) {
    const char *pattern = (cmd->argc > 0) ? cmd->args[0] : "**";

    /* Handle "show" with no args as "show **" (all params) */
    if (pattern[0] == '\0') {
        pattern = "**";
    }

    /* Use pattern matching */
    config_foreach_matching(pattern, show_param_visitor, NULL);

    return CONSOLE_OK;
}

/**
 * @brief Strip quotes from value if present
 */
static const char *strip_quotes(const char *s, char *buf, size_t buflen) {
    if (s == NULL || buflen == 0) return s;
    size_t len = strlen(s);

    /* Check for leading/trailing quotes */
    if (len >= 2 && ((s[0] == '"' && s[len-1] == '"') ||
                     (s[0] == '\'' && s[len-1] == '\''))) {
        if (len - 2 < buflen) {
            memcpy(buf, s + 1, len - 2);
            buf[len - 2] = '\0';
            return buf;
        }
    }
    return s;
}

/**
 * @brief set <path> <value> - Set parameter by path
 *
 * Examples:
 *   set keyer.wpm 25
 *   set audio.sidetone_freq_hz 700
 *   set wifi.ssid=MyNetwork
 *   set wifi.ssid = "My Network"
 *   set wpm 25  (legacy, still works)
 */
static console_error_t cmd_set(const console_parsed_cmd_t *cmd) {
    if (cmd->argc < 1) {
        return CONSOLE_ERR_MISSING_ARG;
    }

    static char path_buf[64];
    static char value_buf[128];
    const char *path = NULL;
    const char *value = NULL;

    /* Check for key=value format in first arg */
    const char *eq = strchr(cmd->args[0], '=');
    if (eq != NULL) {
        /* key=value or key="value" format */
        size_t path_len = (size_t)(eq - cmd->args[0]);
        if (path_len >= sizeof(path_buf)) path_len = sizeof(path_buf) - 1;
        memcpy(path_buf, cmd->args[0], path_len);
        path_buf[path_len] = '\0';
        path = path_buf;
        value = strip_quotes(eq + 1, value_buf, sizeof(value_buf));
    } else if (cmd->argc >= 2) {
        path = cmd->args[0];
        /* Check if args[1] is "=" */
        if (strcmp(cmd->args[1], "=") == 0) {
            if (cmd->argc < 3) {
                return CONSOLE_ERR_MISSING_ARG;
            }
            value = strip_quotes(cmd->args[2], value_buf, sizeof(value_buf));
        } else {
            /* Skip leading = if present */
            const char *v = cmd->args[1];
            if (v[0] == '=') v++;
            value = strip_quotes(v, value_buf, sizeof(value_buf));
        }
    } else {
        return CONSOLE_ERR_MISSING_ARG;
    }

    /* Try full path first (config_find_param handles both path and name) */
    int ret = config_set_param_str(path, value);

    /* If not found and no dot, try legacy name lookup */
    if (ret == -1 && strchr(path, '.') == NULL) {
        /* Search all params for matching name */
        for (size_t i = 0; i < CONSOLE_PARAM_COUNT; i++) {
            if (strcmp(CONSOLE_PARAMS[i].name, path) == 0) {
                ret = config_set_param_str(CONSOLE_PARAMS[i].full_path, value);
                break;
            }
        }
    }

    switch (ret) {
        case 0:
            /* Show confirmation with new value */
            {
                char buf[128];
                if (config_get_param_str(path, buf, sizeof(buf)) == 0) {
                    printf("%s=%s\r\n", path, buf);
                }
            }
            return CONSOLE_OK;
        case -1:
            return CONSOLE_ERR_UNKNOWN_CMD;
        case -2:
            return CONSOLE_ERR_INVALID_VALUE;
        case -4:
            return CONSOLE_ERR_OUT_OF_RANGE;
        default:
            return CONSOLE_ERR_INVALID_VALUE;
    }
}

/**
 * @brief log - Set log level
 */
static console_error_t cmd_log(const console_parsed_cmd_t *cmd) {
#ifdef ESP_PLATFORM
    if (cmd->argc == 0) {
        /* Show current level */
        printf("Log level: %s\r\n", log_level_str(usb_log_get_level()));
        return CONSOLE_OK;
    }

    /* Parse "level TAG LEVEL" or "TAG=L" format */
    const char *arg = cmd->args[0];

    if (strcmp(arg, "level") == 0) {
        /* ESP-IDF style: log level TAG LEVEL */
        if (cmd->argc < 3) {
            return CONSOLE_ERR_MISSING_ARG;
        }
        const char *tag = cmd->args[1];
        const char *level_str = cmd->args[2];

        log_level_t level;
        if (strcmp(level_str, "ERROR") == 0 || strcmp(level_str, "E") == 0) {
            level = LOG_LEVEL_ERROR;
        } else if (strcmp(level_str, "WARN") == 0 || strcmp(level_str, "W") == 0) {
            level = LOG_LEVEL_WARN;
        } else if (strcmp(level_str, "INFO") == 0 || strcmp(level_str, "I") == 0) {
            level = LOG_LEVEL_INFO;
        } else if (strcmp(level_str, "DEBUG") == 0 || strcmp(level_str, "D") == 0) {
            level = LOG_LEVEL_DEBUG;
        } else if (strcmp(level_str, "TRACE") == 0 || strcmp(level_str, "T") == 0) {
            level = LOG_LEVEL_TRACE;
        } else {
            return CONSOLE_ERR_INVALID_VALUE;
        }

        if (strcmp(tag, "*") == 0) {
            usb_log_set_level(level);
        } else {
            usb_log_set_tag_level(tag, level);
        }
        printf("Log %s = %s\r\n", tag, log_level_str(level));
        return CONSOLE_OK;
    }

    /* Compact style: TAG=L */
    char tag_buf[32];
    strncpy(tag_buf, arg, sizeof(tag_buf) - 1);
    tag_buf[sizeof(tag_buf) - 1] = '\0';

    char *eq = strchr(tag_buf, '=');
    if (eq != NULL) {
        *eq = '\0';
        const char *tag = tag_buf;
        const char *level_char = eq + 1;

        log_level_t level;
        switch (*level_char) {
            case 'E': level = LOG_LEVEL_ERROR; break;
            case 'W': level = LOG_LEVEL_WARN; break;
            case 'I': level = LOG_LEVEL_INFO; break;
            case 'D': level = LOG_LEVEL_DEBUG; break;
            case 'T': level = LOG_LEVEL_TRACE; break;
            default: return CONSOLE_ERR_INVALID_VALUE;
        }

        if (strcmp(tag, "*") == 0) {
            usb_log_set_level(level);
        } else {
            usb_log_set_tag_level(tag, level);
        }
        printf("Log %s = %s\r\n", tag, log_level_str(level));
        return CONSOLE_OK;
    }

    return CONSOLE_ERR_INVALID_VALUE;
#else
    (void)cmd;
    printf("log not available on host\r\n");
    return CONSOLE_OK;
#endif
}

/**
 * @brief uf2 - Enter UF2 bootloader mode
 */
static console_error_t cmd_uf2(const console_parsed_cmd_t *cmd) {
    (void)cmd;
#ifdef ESP_PLATFORM
    printf("Entering UF2 bootloader mode...\r\n");
    vTaskDelay(pdMS_TO_TICKS(100));
    usb_uf2_enter();
    /* Does not return */
#else
    printf("uf2 not available on host\r\n");
#endif
    return CONSOLE_OK;
}

/**
 * @brief factory-reset confirm - Erase NVS and reboot
 */
static console_error_t cmd_factory_reset(const console_parsed_cmd_t *cmd) {
    if (cmd->argc == 0 || strcmp(cmd->args[0], "confirm") != 0) {
        return CONSOLE_ERR_REQUIRES_CONFIRM;
    }
#ifdef ESP_PLATFORM
    printf("Erasing NVS and rebooting...\r\n");

    /* Erase the keyer config namespace */
    nvs_handle_t handle;
    esp_err_t err = nvs_open(CONFIG_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        nvs_erase_all(handle);
        nvs_commit(handle);
        nvs_close(handle);
    }

    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
#else
    printf("factory-reset not available on host\r\n");
#endif
    return CONSOLE_OK;
}

/**
 * @brief debug - Set ESP-IDF log levels
 *
 * Supports:
 * - debug none           - disable all logging
 * - debug * verbose      - all verbose
 * - debug wifi warn      - set wifi tag to warn
 * - debug info           - show RT log buffer status (stub)
 */
static console_error_t cmd_debug(const console_parsed_cmd_t *cmd) {
#ifdef ESP_PLATFORM
    if (cmd->argc == 0) {
        /* Show brief RT log status */
        uint32_t rt_dropped = log_stream_dropped(&g_rt_log_stream);
        printf("RT Log: %lu dropped (use 'debug info' for details)\r\n",
               (unsigned long)rt_dropped);
        return CONSOLE_OK;
    }

    const char *arg1 = cmd->args[0];

    /* debug info - show RT log buffer status */
    if (strcmp(arg1, "info") == 0) {
        uint32_t rt_count = log_stream_count(&g_rt_log_stream);
        uint32_t rt_dropped = log_stream_dropped(&g_rt_log_stream);
        uint32_t bg_count = log_stream_count(&g_bg_log_stream);
        uint32_t bg_dropped = log_stream_dropped(&g_bg_log_stream);

        printf("RT Log:  %lu/%d entries, %lu dropped\r\n",
               (unsigned long)rt_count, LOG_BUFFER_SIZE, (unsigned long)rt_dropped);
        printf("BG Log:  %lu/%d entries, %lu dropped\r\n",
               (unsigned long)bg_count, LOG_BUFFER_SIZE, (unsigned long)bg_dropped);
        printf("Diag:    %s\r\n",
               atomic_load_explicit(&g_rt_diag_enabled, memory_order_relaxed) ? "ON" : "OFF");
        return CONSOLE_OK;
    }

    /* debug none - disable all logging */
    if (strcmp(arg1, "none") == 0) {
        esp_log_level_set("*", ESP_LOG_NONE);
        printf("All logging disabled\r\n");
        return CONSOLE_OK;
    }

    /* debug <tag> <level> */
    if (cmd->argc < 2) {
        return CONSOLE_ERR_MISSING_ARG;
    }

    const char *tag = arg1;
    const char *level_str = cmd->args[1];
    esp_log_level_t level;

    if (strcmp(level_str, "none") == 0) {
        level = ESP_LOG_NONE;
    } else if (strcmp(level_str, "error") == 0) {
        level = ESP_LOG_ERROR;
    } else if (strcmp(level_str, "warn") == 0) {
        level = ESP_LOG_WARN;
    } else if (strcmp(level_str, "info") == 0) {
        level = ESP_LOG_INFO;
    } else if (strcmp(level_str, "debug") == 0) {
        level = ESP_LOG_DEBUG;
    } else if (strcmp(level_str, "verbose") == 0) {
        level = ESP_LOG_VERBOSE;
    } else {
        return CONSOLE_ERR_INVALID_VALUE;
    }

    esp_log_level_set(tag, level);
    printf("Log %s = %s\r\n", tag, level_str);
    return CONSOLE_OK;
#else
    (void)cmd;
    printf("debug not available on host\r\n");
    return CONSOLE_OK;
#endif
}

/**
 * @brief diag [on|off] - Toggle RT diagnostic logging
 */
static console_error_t cmd_diag(const console_parsed_cmd_t *cmd) {
    if (cmd->argc == 0) {
        /* Show current state */
        bool enabled = atomic_load_explicit(&g_rt_diag_enabled, memory_order_relaxed);
        printf("Diagnostic logging: %s\r\n", enabled ? "ON" : "OFF");
        return CONSOLE_OK;
    }

    const char *arg = cmd->args[0];
    if (strcmp(arg, "on") == 0) {
        atomic_store_explicit(&g_rt_diag_enabled, true, memory_order_relaxed);
        printf("Diagnostic logging: ON\r\n");
    } else if (strcmp(arg, "off") == 0) {
        atomic_store_explicit(&g_rt_diag_enabled, false, memory_order_relaxed);
        printf("Diagnostic logging: OFF\r\n");
    } else {
        return CONSOLE_ERR_INVALID_VALUE;
    }
    return CONSOLE_OK;
}

/**
 * @brief gpio - Read raw GPIO state for debugging
 */
static console_error_t cmd_gpio(const console_parsed_cmd_t *cmd) {
#ifdef ESP_PLATFORM
    /* Check for pin argument: gpio <pin> to test a specific pin */
    if (cmd->argc > 0) {
        /* gpio init - reinitialize GPIO with logging */
        if (strcmp(cmd->args[0], "init") == 0) {
            printf("Re-initializing GPIO pins 3, 4, 15...\r\n");

            /* Configure DIT (GPIO 3) */
            gpio_config_t dit_conf = {
                .pin_bit_mask = (1ULL << 3),
                .mode = GPIO_MODE_INPUT,
                .pull_up_en = GPIO_PULLUP_ENABLE,
                .pull_down_en = GPIO_PULLDOWN_DISABLE,
                .intr_type = GPIO_INTR_DISABLE,
            };
            esp_err_t err = gpio_config(&dit_conf);
            printf("GPIO3 (DIT): %s\r\n", esp_err_to_name(err));

            /* Configure DAH (GPIO 4) */
            gpio_config_t dah_conf = {
                .pin_bit_mask = (1ULL << 4),
                .mode = GPIO_MODE_INPUT,
                .pull_up_en = GPIO_PULLUP_ENABLE,
                .pull_down_en = GPIO_PULLDOWN_DISABLE,
                .intr_type = GPIO_INTR_DISABLE,
            };
            err = gpio_config(&dah_conf);
            printf("GPIO4 (DAH): %s\r\n", esp_err_to_name(err));

            /* Configure TX (GPIO 15) */
            gpio_config_t tx_conf = {
                .pin_bit_mask = (1ULL << 15),
                .mode = GPIO_MODE_OUTPUT,
                .pull_up_en = GPIO_PULLUP_DISABLE,
                .pull_down_en = GPIO_PULLDOWN_DISABLE,
                .intr_type = GPIO_INTR_DISABLE,
            };
            err = gpio_config(&tx_conf);
            printf("GPIO15 (TX): %s\r\n", esp_err_to_name(err));

            /* Read levels after config */
            printf("\r\nAfter config:\r\n");
            printf("  GPIO3 = %d\r\n", gpio_get_level(3));
            printf("  GPIO4 = %d\r\n", gpio_get_level(4));
            printf("  GPIO15 = %d\r\n", gpio_get_level(15));
            return CONSOLE_OK;
        }

        int pin = atoi(cmd->args[0]);
        if (pin < 0 || pin > 48) {
            printf("Invalid pin (0-48) or use 'gpio init'\r\n");
            return CONSOLE_ERR_INVALID_VALUE;
        }

        /* Configure pin as input with pull-up */
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << pin),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        esp_err_t err = gpio_config(&io_conf);
        if (err != ESP_OK) {
            printf("gpio_config failed: %s\r\n", esp_err_to_name(err));
            return CONSOLE_OK;
        }

        printf("GPIO%d configured with pull-up. Reading...\r\n", pin);
        for (int i = 0; i < 5; i++) {
            int level = gpio_get_level((gpio_num_t)pin);
            printf("  GPIO%d = %d\r\n", pin, level);
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        printf("With pull-up: should read 1 when floating, 0 when shorted to GND\r\n");
        return CONSOLE_OK;
    }

    /* Get HAL config to see which pins it's actually using */
    hal_gpio_config_t hal_cfg = hal_gpio_get_config();

    /* Read from pins HAL thinks it's using */
    int dit_raw = gpio_get_level(hal_cfg.dit_pin);
    int dah_raw = gpio_get_level(hal_cfg.dah_pin);
    int tx_raw = gpio_get_level(hal_cfg.tx_pin);

    /* Also read hardcoded GPIO 3, 4 directly for comparison */
    int gpio3_direct = gpio_get_level(3);
    int gpio4_direct = gpio_get_level(4);
    printf("Direct read: GPIO3=%d  GPIO4=%d\r\n", gpio3_direct, gpio4_direct);

    /* Read via HAL (with active_low logic applied) */
    gpio_state_t state = hal_gpio_read_paddles();

    /* Call hal_gpio_read_paddles() again and capture result separately */
    gpio_state_t state2 = hal_gpio_read_paddles();

    printf("HAL Config: DIT=%d, DAH=%d, TX=%d\r\n",
           hal_cfg.dit_pin, hal_cfg.dah_pin, hal_cfg.tx_pin);
    printf("GPIO Raw:  DIT(%d)=%d  DAH(%d)=%d  TX(%d)=%d\r\n",
           hal_cfg.dit_pin, dit_raw, hal_cfg.dah_pin, dah_raw, hal_cfg.tx_pin, tx_raw);
    printf("HAL State: dit=%d  dah=%d  (active_low=%d)\r\n",
           gpio_dit(state) ? 1 : 0, gpio_dah(state) ? 1 : 0, hal_cfg.active_low);
    printf("state.bits = 0x%02X  state2.bits = 0x%02X\r\n", state.bits, state2.bits);

    /* Debug: Print address of hal_gpio_read_paddles */
    printf("hal_gpio_read_paddles @ %p\r\n", (void*)hal_gpio_read_paddles);

    /* Manual calculation for debug */
    bool dit_pressed = hal_cfg.active_low ? (dit_raw == 0) : (dit_raw != 0);
    bool dah_pressed = hal_cfg.active_low ? (dah_raw == 0) : (dah_raw != 0);
    printf("Manual calc: dit_pressed=%d dah_pressed=%d\r\n", dit_pressed, dah_pressed);

    /* Debug: manually create gpio_state to compare */
    gpio_state_t manual_state = gpio_from_paddles(dit_pressed, dah_pressed);
    printf("Manual gpio_from_paddles: bits=0x%02X dit=%d dah=%d\r\n",
           manual_state.bits, gpio_dit(manual_state), gpio_dah(manual_state));
    printf("\r\nWith pull-up enabled, floating pins should read 1.\r\n");
    printf("If reading 0 when floating, the pin may be:\r\n");
    printf("  - Used by PSRAM/Flash (check your board)\r\n");
    printf("  - Shorted to GND on PCB\r\n");
    printf("  - Not a valid GPIO on this chip\r\n");
    printf("\r\nTry: gpio init  or  gpio <pin>\r\n");

    return CONSOLE_OK;
#else
    (void)cmd;
    printf("gpio not available on host\r\n");
    return CONSOLE_OK;
#endif
}

/**
 * @brief test - Diagnostic test commands
 */
static console_error_t cmd_test(const console_parsed_cmd_t *cmd) {
#ifdef ESP_PLATFORM
    if (cmd->argc == 0) {
        printf("Usage: test cdc1 | test log | test gpio\r\n");
        return CONSOLE_OK;
    }

    const char *arg = cmd->args[0];

    if (strcmp(arg, "gpio") == 0) {
        /* Write GPIO debug info directly to CDC1 */
        char buf[256];
        hal_gpio_config_t cfg = hal_gpio_get_config();

        /* Print config once */
        int len = snprintf(buf, sizeof(buf),
            "CONFIG: dit=%d dah=%d tx=%d active_low=%d\r\n",
            cfg.dit_pin, cfg.dah_pin, cfg.tx_pin, cfg.active_low);
        tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_1, (const uint8_t *)buf, (size_t)len);
        tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_1, 0);

        /* Test gpio_from_paddles directly */
        gpio_state_t test1 = gpio_from_paddles(true, false);
        gpio_state_t test2 = gpio_from_paddles(false, true);
        gpio_state_t test3 = gpio_from_paddles(true, true);
        len = snprintf(buf, sizeof(buf),
            "gpio_from_paddles: (1,0)=0x%02X (0,1)=0x%02X (1,1)=0x%02X\r\n",
            test1.bits, test2.bits, test3.bits);
        tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_1, (const uint8_t *)buf, (size_t)len);
        tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_1, 0);

        for (int i = 0; i < 10; i++) {
            int dit_raw = gpio_get_level(cfg.dit_pin);
            int dah_raw = gpio_get_level(cfg.dah_pin);
            gpio_state_t state = hal_gpio_read_paddles();

            bool dit_pressed = cfg.active_low ? (dit_raw == 0) : (dit_raw != 0);
            bool dah_pressed = cfg.active_low ? (dah_raw == 0) : (dah_raw != 0);

            /* Also create state manually to compare */
            gpio_state_t manual = gpio_from_paddles(dit_pressed, dah_pressed);

            len = snprintf(buf, sizeof(buf),
                "[%d] raw=%d,%d pressed=%d,%d HAL=0x%02X manual=0x%02X\r\n",
                i, dit_raw, dah_raw, dit_pressed, dah_pressed, state.bits, manual.bits);

            tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_1, (const uint8_t *)buf, (size_t)len);
            tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_1, 0);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        printf("Done. Check CDC1.\r\n");
        return CONSOLE_OK;
    }

    if (strcmp(arg, "cdc1") == 0) {
        /* Write directly to CDC1 */
        const char *msg = "=== CDC1 TEST MESSAGE ===\r\n";
        size_t len = strlen(msg);

        printf("Writing to CDC1 (connected=%d)...\r\n",
               usb_cdc_connected(CDC_ITF_LOG));

        tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_1, (const uint8_t *)msg, len);
        tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_1, 0);

        printf("Done. Check CDC1 terminal.\r\n");
        return CONSOLE_OK;
    }

    if (strcmp(arg, "log") == 0) {
        /* Push test entry to log stream */
        int64_t now_us = esp_timer_get_time();
        RT_INFO(&g_rt_log_stream, now_us, "TEST LOG ENTRY");

        printf("Pushed test entry to RT log stream.\r\n");
        printf("Stream count: %lu\r\n",
               (unsigned long)log_stream_count(&g_rt_log_stream));
        return CONSOLE_OK;
    }

    return CONSOLE_ERR_INVALID_VALUE;
#else
    (void)cmd;
    printf("test not available on host\r\n");
    return CONSOLE_OK;
#endif
}

/**
 * @brief decoder - CW decoder control and status
 */
static console_error_t cmd_decoder(const console_parsed_cmd_t *cmd) {
    if (cmd->argc == 0) {
        /* Show status and last decoded text */
        bool enabled = decoder_is_enabled();
        uint32_t wpm = decoder_get_wpm();
        size_t buf_count = decoder_get_buffer_count();
        size_t buf_cap = decoder_get_buffer_capacity();

        printf("Decoder: %s", enabled ? "ON" : "OFF");
        if (wpm > 0) {
            printf(", WPM: %lu", (unsigned long)wpm);
        }
        printf(", buffer: %u/%u chars\r\n", (unsigned)buf_count, (unsigned)buf_cap);

        if (buf_count > 0) {
            char text[65];
            size_t len = decoder_get_text(text, sizeof(text));
            if (len > 0) {
                printf("Last: \"%s\"\r\n", text);
            }
        }

        /* Show current pattern if receiving */
        if (decoder_get_state() == DECODER_STATE_RECEIVING) {
            char pattern[16];
            decoder_get_current_pattern(pattern, sizeof(pattern));
            printf("Pattern: %s\r\n", pattern);
        }

        return CONSOLE_OK;
    }

    const char *arg = cmd->args[0];

    /* decoder on|off */
    if (strcmp(arg, "on") == 0) {
        decoder_set_enabled(true);
        printf("Decoder: ON\r\n");
        return CONSOLE_OK;
    }
    if (strcmp(arg, "off") == 0) {
        decoder_set_enabled(false);
        printf("Decoder: OFF\r\n");
        return CONSOLE_OK;
    }

    /* decoder clear */
    if (strcmp(arg, "clear") == 0) {
        decoder_reset();
        printf("Decoder reset\r\n");
        return CONSOLE_OK;
    }

    /* decoder text - show full buffer with timestamps */
    if (strcmp(arg, "text") == 0) {
        size_t count = decoder_get_buffer_count();
        if (count == 0) {
            printf("(empty)\r\n");
            return CONSOLE_OK;
        }

        decoded_char_t chars[128];
        size_t n = decoder_get_text_with_timestamps(chars, 128);
        for (size_t i = 0; i < n; i++) {
            int64_t ts = chars[i].timestamp_us;
            int secs = (int)(ts / 1000000);
            int ms = (int)((ts / 1000) % 1000);
            char c = chars[i].character;
            if (c == ' ') {
                printf("[%d.%03d] (space)\r\n", secs, ms);
            } else {
                printf("[%d.%03d] %c\r\n", secs, ms, c);
            }
        }
        return CONSOLE_OK;
    }

    /* decoder stats - show timing statistics */
    if (strcmp(arg, "stats") == 0) {
        const timing_classifier_t *tc = decoder_get_timing();
        decoder_stats_t stats;
        decoder_get_stats(&stats);

        uint32_t wpm = decoder_get_wpm();
        float ratio = timing_classifier_get_ratio(tc);

        printf("WPM: %lu (dit: %lldms, dah: %lldms, ratio: %.2f)\r\n",
               (unsigned long)wpm,
               (long long)(tc->dit_avg_us / 1000),
               (long long)(tc->dah_avg_us / 1000),
               (double)ratio);
        printf("Samples: dit=%lu, dah=%lu\r\n",
               (unsigned long)tc->dit_count,
               (unsigned long)tc->dah_count);
        printf("Decoded: %lu chars, %lu words, %lu errors\r\n",
               (unsigned long)stats.chars_decoded,
               (unsigned long)stats.words_decoded,
               (unsigned long)stats.errors);
        printf("Buffer: %u/%u chars\r\n",
               (unsigned)decoder_get_buffer_count(),
               (unsigned)decoder_get_buffer_capacity());
        printf("State: %s\r\n", decoder_state_str(decoder_get_state()));

        return CONSOLE_OK;
    }

    return CONSOLE_ERR_INVALID_VALUE;
}

/* ============================================================================
 * Text Keyer Commands
 * ============================================================================ */

/**
 * @brief send <text> - Send text as CW
 */
static console_error_t cmd_send(const console_parsed_cmd_t *cmd) {
    if (cmd->argc == 0) {
        return CONSOLE_ERR_MISSING_ARG;
    }

    /* Concatenate all arguments with spaces */
    static char text[TEXT_KEYER_MAX_LEN];
    text[0] = '\0';

    for (int i = 0; i < cmd->argc && cmd->args[i] != NULL; i++) {
        if (i > 0) {
            strncat(text, " ", sizeof(text) - strlen(text) - 1);
        }
        strncat(text, cmd->args[i], sizeof(text) - strlen(text) - 1);
    }

    if (text_keyer_send(text) != 0) {
        printf("Error: already sending or invalid text\r\n");
        return CONSOLE_ERR_INVALID_VALUE;
    }

    printf("Sending: %s\r\n", text);
    return CONSOLE_OK;
}

/**
 * @brief m1-m8 - Send memory slot
 */
static console_error_t cmd_memory_send(const console_parsed_cmd_t *cmd) {
    /* Extract slot number from command name (m1 -> 0, m8 -> 7) */
    const char *name = cmd->command;
    if (name[0] != 'm' || name[1] < '1' || name[1] > '8') {
        return CONSOLE_ERR_UNKNOWN_CMD;
    }

    uint8_t slot = (uint8_t)(name[1] - '1');

    text_memory_slot_t mem;
    if (text_memory_get(slot, &mem) != 0) {
        printf("Slot %d is empty\r\n", slot + 1);
        return CONSOLE_ERR_INVALID_VALUE;
    }

    if (text_keyer_send(mem.text) != 0) {
        printf("Error: already sending\r\n");
        return CONSOLE_ERR_INVALID_VALUE;
    }

    printf("Sending M%d [%s]: %s\r\n", slot + 1, mem.label, mem.text);
    return CONSOLE_OK;
}

/**
 * @brief abort - Abort current transmission
 */
static console_error_t cmd_abort(const console_parsed_cmd_t *cmd) {
    (void)cmd;
    text_keyer_abort();
    printf("Aborted\r\n");
    return CONSOLE_OK;
}

/**
 * @brief pause - Pause transmission
 */
static console_error_t cmd_pause(const console_parsed_cmd_t *cmd) {
    (void)cmd;
    text_keyer_pause();
    printf("Paused\r\n");
    return CONSOLE_OK;
}

/**
 * @brief resume - Resume transmission
 */
static console_error_t cmd_resume(const console_parsed_cmd_t *cmd) {
    (void)cmd;
    text_keyer_resume();
    printf("Resumed\r\n");
    return CONSOLE_OK;
}

/**
 * @brief mem [slot] [text|clear|label <label>] - Memory slot management
 */
static console_error_t cmd_mem(const console_parsed_cmd_t *cmd) {
    /* No args - list all slots */
    if (cmd->argc == 0) {
        for (uint8_t i = 0; i < TEXT_MEMORY_SLOTS; i++) {
            text_memory_slot_t slot;
            if (text_memory_get(i, &slot) == 0) {
                printf("M%d [%s]: %s\r\n", i + 1, slot.label, slot.text);
            } else {
                printf("M%d: (empty)\r\n", i + 1);
            }
        }
        return CONSOLE_OK;
    }

    /* Parse slot number */
    int slot_num = atoi(cmd->args[0]);
    if (slot_num < 1 || slot_num > 8) {
        printf("Error: slot must be 1-8\r\n");
        return CONSOLE_ERR_OUT_OF_RANGE;
    }
    uint8_t slot = (uint8_t)(slot_num - 1);

    /* Just slot number - show that slot */
    if (cmd->argc == 1) {
        text_memory_slot_t mem;
        if (text_memory_get(slot, &mem) == 0) {
            printf("M%d [%s]: %s\r\n", slot + 1, mem.label, mem.text);
        } else {
            printf("M%d: (empty)\r\n", slot + 1);
        }
        return CONSOLE_OK;
    }

    /* Check for subcommands */
    if (strcmp(cmd->args[1], "clear") == 0) {
        text_memory_clear(slot);
        printf("M%d cleared\r\n", slot + 1);
        return CONSOLE_OK;
    }

    if (strcmp(cmd->args[1], "label") == 0) {
        if (cmd->argc < 3 || cmd->args[2] == NULL) {
            return CONSOLE_ERR_MISSING_ARG;
        }
        text_memory_set_label(slot, cmd->args[2]);
        printf("M%d label set to '%s'\r\n", slot + 1, cmd->args[2]);
        return CONSOLE_OK;
    }

    /* Remaining args are the text */
    static char text[TEXT_KEYER_MAX_LEN];
    text[0] = '\0';

    for (int i = 1; i < cmd->argc && cmd->args[i] != NULL; i++) {
        if (i > 1) {
            strncat(text, " ", sizeof(text) - strlen(text) - 1);
        }
        strncat(text, cmd->args[i], sizeof(text) - strlen(text) - 1);
    }

    text_memory_set(slot, text, NULL);
    printf("M%d saved\r\n", slot + 1);
    return CONSOLE_OK;
}

/* ============================================================================
 * Command registry
 * ============================================================================ */

/* Usage strings for commands with non-trivial syntax */
static const char USAGE_LOG[] =
    "  log                 Show current level\r\n"
    "  log level * LEVEL   Set all tags (ERROR/WARN/INFO/DEBUG/TRACE)\r\n"
    "  log level TAG LEVEL Set specific tag\r\n"
    "  log *=L             Compact: set all (E/W/I/D/T)\r\n"
    "  log TAG=L           Compact: set tag";

static const char USAGE_STATS[] =
    "  stats               Overview (uptime, heap, stream)\r\n"
    "  stats heap          Heap memory details\r\n"
    "  stats tasks         Task list by core\r\n"
    "  stats stream        Stream buffer status\r\n"
    "  stats rt            RT task statistics";

static const char USAGE_SHOW[] =
    "  show                  All parameters\r\n"
    "  show keyer.*          All keyer parameters\r\n"
    "  show keyer.**         Keyer + subfamilies\r\n"
    "  show keyer.wpm        Single parameter\r\n"
    "  show hw.*             Alias for hardware.*\r\n"
    "\r\n"
    "Families:\r\n"
    "  keyer (k)      Keying behavior\r\n"
    "  audio (a,snd)  Sidetone output\r\n"
    "  hardware (hw)  GPIO pins\r\n"
    "  timing (t)     RT loop, PTT\r\n"
    "  system (sys)   Debug, LED";

static const char USAGE_SET[] =
    "  set <path> <value>  Set parameter value\r\n"
    "\r\n"
    "Examples:\r\n"
    "  set keyer.wpm 25\r\n"
    "  set audio.sidetone_freq_hz 700\r\n"
    "  set wpm 25              (legacy shorthand)";

static const char USAGE_DEBUG[] =
    "  debug               Show RT log status\r\n"
    "  debug info          Show RT log buffer details\r\n"
    "  debug none          Disable all ESP-IDF logging\r\n"
    "  debug <tag> <level> Set tag level (none/error/warn/info/debug/verbose)";

static const char USAGE_DIAG[] =
    "  diag                Show diagnostic state\r\n"
    "  diag on             Enable RT diagnostic logging\r\n"
    "  diag off            Disable RT diagnostic logging";

static const char USAGE_DECODER[] =
    "  decoder             Show status and last decoded text\r\n"
    "  decoder on|off      Enable/disable decoder\r\n"
    "  decoder text        Show buffer with timestamps\r\n"
    "  decoder stats       Show timing statistics\r\n"
    "  decoder clear       Clear buffer and reset timing";

static const char USAGE_SEND[] =
    "  send <text>         Send text as CW\r\n"
    "\r\n"
    "Supports A-Z, 0-9, punctuation, spaces, and prosigns.\r\n"
    "Prosigns: <SK>, <AR>, <BT>, <KN>, <AS>, <SN>, <KA>\r\n"
    "\r\n"
    "Examples:\r\n"
    "  send CQ CQ DE IU3QEZ K\r\n"
    "  send 73 <SK>";

static const char USAGE_MEM[] =
    "  mem                 List all slots\r\n"
    "  mem <slot>          Show slot (1-8)\r\n"
    "  mem <slot> <text>   Save text to slot\r\n"
    "  mem <slot> clear    Clear slot\r\n"
    "  mem <slot> label X  Set slot label";

static const console_cmd_t s_commands[] = {
    { "help",          "List commands or show help",   NULL,        cmd_help },
    { "?",             "Alias for help",               NULL,        cmd_question },
    { "version",       "Show version info",            NULL,        cmd_version },
    { "v",             "Alias for version",            NULL,        cmd_version },
    { "stats",         "System statistics",            USAGE_STATS, cmd_stats },
    { "show",          "Show parameters",              USAGE_SHOW,  cmd_show },
    { "set",           "Set parameter value",          USAGE_SET,   cmd_set },
    { "save",          "Persist to NVS",               NULL,        cmd_save },
    { "reboot",        "Restart system",               NULL,        cmd_reboot },
    { "log",           "Set log level",                USAGE_LOG,   cmd_log },
    { "debug",         "Set ESP-IDF log levels",       USAGE_DEBUG, cmd_debug },
    { "uf2",           "Enter UF2 bootloader",         NULL,        cmd_uf2 },
    { "flash",         "Enter bootloader mode",        NULL,        cmd_uf2 },
    { "factory-reset", "Erase NVS and reboot",         NULL,        cmd_factory_reset },
    { "diag",          "RT diagnostic logging",        USAGE_DIAG,  cmd_diag },
    { "decoder",       "CW decoder control",           USAGE_DECODER, cmd_decoder },
    { "test",          "Diagnostic tests",             NULL,        cmd_test },
    { "gpio",          "Read raw GPIO state",          NULL,        cmd_gpio },
    { "send",          "Send text as CW",              USAGE_SEND,  cmd_send },
    { "m1",            "Send memory slot 1",           NULL,        cmd_memory_send },
    { "m2",            "Send memory slot 2",           NULL,        cmd_memory_send },
    { "m3",            "Send memory slot 3",           NULL,        cmd_memory_send },
    { "m4",            "Send memory slot 4",           NULL,        cmd_memory_send },
    { "m5",            "Send memory slot 5",           NULL,        cmd_memory_send },
    { "m6",            "Send memory slot 6",           NULL,        cmd_memory_send },
    { "m7",            "Send memory slot 7",           NULL,        cmd_memory_send },
    { "m8",            "Send memory slot 8",           NULL,        cmd_memory_send },
    { "abort",         "Abort CW transmission",        NULL,        cmd_abort },
    { "pause",         "Pause CW transmission",        NULL,        cmd_pause },
    { "resume",        "Resume CW transmission",       NULL,        cmd_resume },
    { "mem",           "Memory slot management",       USAGE_MEM,   cmd_mem },
};

#define NUM_COMMANDS (sizeof(s_commands) / sizeof(s_commands[0]))

const console_cmd_t *console_get_commands(size_t *count) {
    if (count != NULL) {
        *count = NUM_COMMANDS;
    }
    return s_commands;
}

const console_cmd_t *console_find_command(const char *name) {
    if (name == NULL || *name == '\0') {
        return NULL;
    }
    for (size_t i = 0; i < NUM_COMMANDS; i++) {
        if (strcmp(s_commands[i].name, name) == 0) {
            return &s_commands[i];
        }
    }
    return NULL;
}

console_error_t console_execute(const console_parsed_cmd_t *cmd) {
    if (cmd == NULL || cmd->command == NULL || cmd->command[0] == '\0') {
        return CONSOLE_OK; /* Empty line, do nothing */
    }

    const console_cmd_t *c = console_find_command(cmd->command);
    if (c == NULL) {
        return CONSOLE_ERR_UNKNOWN_CMD;
    }

    /* Handle "cmd ?" to show help */
    if (cmd->argc > 0 && cmd->args[0] != NULL && strcmp(cmd->args[0], "?") == 0) {
        show_command_help(c);
        return CONSOLE_OK;
    }

    return c->handler(cmd);
}
