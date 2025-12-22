/**
 * @file commands.c
 * @brief Command registry and handlers
 *
 * Implements all console commands: help, version, stats, reboot, etc.
 */

#include "console.h"
#include "config_console.h"
#include "config_nvs.h"
#include <stdio.h>
#include <string.h>

#ifdef CONFIG_IDF_TARGET
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
/* Use USB console printf for command output */
#define printf usb_console_printf
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
 * @brief help [cmd] - List commands or show help for specific command
 */
static console_error_t cmd_help(const console_parsed_cmd_t *cmd) {
    if (cmd->argc > 0 && cmd->args[0] != NULL) {
        /* Help for specific command */
        const console_cmd_t *c = console_find_command(cmd->args[0]);
        if (c == NULL) {
            return CONSOLE_ERR_UNKNOWN_CMD;
        }
        printf("  %s - %s\r\n", c->name, c->brief);
    } else {
        /* List all commands */
        size_t count;
        const console_cmd_t *cmds = console_get_commands(&count);
        printf("Available commands:\r\n");
        for (size_t i = 0; i < count; i++) {
            printf("  %-14s %s\r\n", cmds[i].name, cmds[i].brief);
        }
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
#ifdef CONFIG_IDF_TARGET
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
#ifdef CONFIG_IDF_TARGET
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
    } else if (strcmp(cmd->args[0], "heap") == 0) {
        uint32_t heap_free = esp_get_free_heap_size();
        uint32_t heap_min = esp_get_minimum_free_heap_size();
        printf("heap free:    %lu bytes\r\n", (unsigned long)heap_free);
        printf("heap minimum: %lu bytes\r\n", (unsigned long)heap_min);
    } else if (strcmp(cmd->args[0], "tasks") == 0) {
        printf("=== Core 0 (RT) ===\r\n");
        printf("NAME            CPU%%  STACK  PRIO\r\n");
        printf("(enable runtime stats for details)\r\n\r\n");
        printf("=== Core 1 (BE) ===\r\n");
        printf("NAME            CPU%%  STACK  PRIO\r\n");
        printf("(enable runtime stats for details)\r\n");
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
#ifdef CONFIG_IDF_TARGET
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
#ifdef CONFIG_IDF_TARGET
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

/**
 * @brief show [pattern] - Show parameters
 */
static console_error_t cmd_show(const console_parsed_cmd_t *cmd) {
    const char *pattern = (cmd->argc > 0) ? cmd->args[0] : NULL;
    size_t pattern_len = pattern ? strlen(pattern) : 0;
    bool has_wildcard = pattern && pattern[pattern_len - 1] == '*';

    if (has_wildcard) {
        pattern_len--;  /* Exclude the wildcard */
    }

    for (size_t i = 0; i < CONSOLE_PARAM_COUNT; i++) {
        const param_descriptor_t *p = &CONSOLE_PARAMS[i];
        bool match = false;

        if (pattern == NULL) {
            match = true;
        } else if (has_wildcard) {
            match = (strncmp(p->name, pattern, pattern_len) == 0) ||
                    (strncmp(p->category, pattern, pattern_len) == 0);
        } else {
            match = (strcmp(p->name, pattern) == 0);
        }

        if (match) {
            char buf[32];
            config_get_param_str(p->name, buf, sizeof(buf));
            printf("%s=%s\r\n", p->name, buf);
        }
    }
    return CONSOLE_OK;
}

/**
 * @brief set <param> <value> - Set parameter value
 */
static console_error_t cmd_set(const console_parsed_cmd_t *cmd) {
    if (cmd->argc < 2) {
        return CONSOLE_ERR_MISSING_ARG;
    }

    int ret = config_set_param_str(cmd->args[0], cmd->args[1]);
    switch (ret) {
        case 0:
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
#ifdef CONFIG_IDF_TARGET
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
#ifdef CONFIG_IDF_TARGET
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
#ifdef CONFIG_IDF_TARGET
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
#ifdef CONFIG_IDF_TARGET
    if (cmd->argc == 0) {
        /* Show current RT log status */
        printf("RT Log: (use 'debug info' for details)\r\n");
        return CONSOLE_OK;
    }

    const char *arg1 = cmd->args[0];

    /* debug info - show RT log buffer status */
    if (strcmp(arg1, "info") == 0) {
        printf("RT Log: buffer status\r\n");
        printf("(RT log stats not yet implemented)\r\n");
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

/* ============================================================================
 * Command registry
 * ============================================================================ */

static const console_cmd_t s_commands[] = {
    { "help",          "List commands or show help",   cmd_help },
    { "?",             "Alias for help",               cmd_question },
    { "version",       "Show version info",            cmd_version },
    { "v",             "Alias for version",            cmd_version },
    { "stats",         "System statistics",            cmd_stats },
    { "show",          "Show parameters",              cmd_show },
    { "set",           "Set parameter value",          cmd_set },
    { "save",          "Persist to NVS",               cmd_save },
    { "reboot",        "Restart system",               cmd_reboot },
    { "log",           "Set log level",                cmd_log },
    { "debug",         "Set ESP-IDF log levels",       cmd_debug },
    { "uf2",           "Enter UF2 bootloader",         cmd_uf2 },
    { "flash",         "Enter bootloader mode",        cmd_uf2 },
    { "factory-reset", "Erase NVS and reboot",         cmd_factory_reset },
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

    return c->handler(cmd);
}
